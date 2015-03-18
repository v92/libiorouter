import os
import socket
import errno
import signal
import argparse
import logging
import logging.handlers
import sys
import traceback
import threading
import Queue
import json
import subprocess
from time import time
from collections import deque, Counter
import ConfigParser
import msgpack

import zmq
from zmq import Context
from zmq.eventloop.zmqstream import ZMQStream
from zmq.eventloop.ioloop import PeriodicCallback
from zmq.eventloop.ioloop import IOLoop
from tornado.netutil import set_close_exec


CLIENT_TIMEOUT = 60  # drop client counters after [sec]
RAMDISK_MOUNTPOINT = '/run'
CONFIG_SECTION = 'deloader'
TOUCH_OPS = ['mkdir', 'touch']
WRITE_OPS = TOUCH_OPS+['write', 'unlink', 'openwr', 'chown', 'chmod']


class Server():
    def __init__(self, sockfile, local, remotes, state_path, journal_path, backup_journal_dir, io_loop=None):
        logging.info('Starting deloader')
        logging.info('Sock: %s' % sockfile)
        logging.info('In: %s Out: %s' % (local, repr(remotes)))
        logging.info('Journal: %s' % journal_path)
        logging.info('Backup journal dir: %s' % backup_journal_dir)
        logging.info('State: %s' % state_path)

        self.incoming_dgram_sock_sockfile = sockfile

        self.state = AsyncState(state_path)
        self.state.run()
        self.journal = AsyncJournalHandler(journal_path)
        self.journal.run()
        self.rm = AsyncRm()
        self.rm.run()
        self.counter = Counter()

        logging.info('State dump: '+repr(self.state.last))

        self.last_id = self.state.last.get('last_id', 0)
        self.last_timestamp = self.state.last.get('last_timestamp', 0)
        self.msg_buffer = self.journal.load_buffer(journal_path, backup_journal_dir, self)

        self.context = Context()
        self.io_loop = io_loop if io_loop is not None else IOLoop.instance()
        self.incoming_dgram_sock = None
        self.zmq_stream = None
        self.zmq_clients = []

        self.workers = {}  # conn_id:(last_id, timestamp)
        self.out_streams = {}  # stream:last_id
        self.out_stream_endpoints = {}

        self.start_incoming_dgram_sock(sockfile)
        self.start_zmq(local)
        self.start_zmq_clients(remotes)

        self._periodic = PeriodicCallback(self.on_timer, 1000)
        self._periodic.start()

    def start_zmq(self, endpoint):
        socket = self.context.socket(zmq.ROUTER)
        socket.bind(endpoint)
        self.zmq_stream = ZMQStream(socket)
        self.zmq_stream.on_recv(self.on_message)

    def start_zmq_clients(self, endpoints):
        saved_states = self.state.last.get('endpoint_counters', {})
        for e in endpoints:
            socket = self.context.socket(zmq.DEALER)
            stream = ZMQStream(socket, self.io_loop)
            def on_msg(msg, s=stream):
                self.on_client_message(s, msg)
            stream.on_recv(on_msg)
            stream.socket.setsockopt(zmq.LINGER, 0)
            stream.connect(e)
            try:
                c = int(saved_states.get(e, 0))
            except:
                c = 0
            stream.send_multipart([b'', b'\x01', str(c)])
            self.out_streams[stream] = c
            self.out_stream_endpoints[stream] = e

    def start_incoming_dgram_sock(self, sockfile):
        if self.incoming_dgram_sock_sockfile.startswith('/'):
            try:
                os.remove(self.incoming_dgram_sock_sockfile)
            except OSError:
                pass
            s = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
            set_close_exec(s.fileno())
            s.setblocking(0)
            s.bind(sockfile)
        else:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            set_close_exec(s.fileno())
            s.setblocking(0)
            ip, port = self.incoming_dgram_sock_sockfile.split(':')
            s.bind((ip, int(port)))

        self.incoming_dgram_sock = s
        def accept_handler(fd, events):
            while True:
                try:
                    data, address = self.incoming_dgram_sock.recvfrom(2500)
                except socket.error, e:
                    if e.args[0] in (errno.EWOULDBLOCK, errno.EAGAIN):
                        return
                    raise
                self.on_recive_dgram(data, address)
        self.io_loop.add_handler(self.incoming_dgram_sock.fileno(), accept_handler, IOLoop.READ)

    def on_recive_dgram(self, data, address):
        # print 'incoming log:', data

        data = data.strip()
        invalidate, hit, operation, path = self.parse_msg(data)
        self.counter[(hit, operation, path)] += 1
        if invalidate:
            self.last_id += 1
            self.last_timestamp = time()
            self.msg_buffer.append((self.last_id, data))
            self.journal.put("%i %s\n" % (self.last_id, data))
            for addr in self.workers:
                self.zmq_stream.send_multipart([addr, b'', b'\x02', str(self.last_id), data])

    def on_message(self, msg):  # ROUTER (clients -> daemon)
        if len(msg) < 3:
            return
        addr = msg[0]
        t = msg[2]
        if t in [b'\x04', b'\x05']:
            if t == b'\x04':
                logging.info('Sending counter info')
                self.zmq_stream.send_multipart([addr, b'', msgpack.packb(self.counter.most_common(1000))])
            if t == b'\x05':
                logging.info('Sending state info')
                self.zmq_stream.send_multipart([addr, b'', msgpack.packb(self.format_state())])
            return
        if len(msg) != 4:
            return
        if t == b'\x01':  # register client and set last received message
            last = int(msg[3])
            self.workers[addr] = last, time()
            if last < self.last_id:
                # start bulk transfer
                for i, data in self.msg_buffer:
                    if i > last:
                        self.zmq_stream.send_multipart([addr, b'', b'\x03', str(i), data])

    def on_client_message(self, stream, msg):  # DEALER (daemon -> clients)
        position = int(msg[2])
        if self.out_streams[stream] + 1 == position or msg[1] == b'\x03':  # x03 is bulk transfer, ignore position
            self.out_streams[stream] = position

        invalidate, hit, operation, path = self.parse_msg(msg[3])
        if invalidate:
            self.rm.put((path, operation))

    def on_timer(self):
        for c, l in self.out_streams.iteritems():
            c.send_multipart([b'', b'\x01', str(l)])

        # cleanup old workers
        for addr, (l, t) in self.workers.items():
            if t + CLIENT_TIMEOUT < time():
                del self.workers[addr]

        self.state.put(json.dumps(self.format_state(), indent=4, sort_keys=True))

        # print 'Last id:', self.last_id, 'In:', dict([(k, v[0]) for k, v in self.workers.iteritems()]),\
        #         'Out:', dict([(n, self.out_streams[e]) for e, n in self.out_stream_endpoints.iteritems()])

    def format_state(self):
        return {
            'last_id': self.last_id,
            'endpoint_counters': dict([(n, self.out_streams[e]) for e, n in self.out_stream_endpoints.iteritems()])
        }

    def parse_msg(self, data):
        parts = data.split(' ', 3)
        if len(parts) != 4:
            return False, None, None
        timestamp, hit, operation, path = parts
        invalidate = operation in WRITE_OPS and path.startswith('/nfsmnt/')  # TODO: update type list
        return invalidate, hit, operation, path

    @staticmethod
    def start_listening():
        """Start listening to new messages
        """
        IOLoop.instance().start()

    @staticmethod
    def stop_listening():
        """Stop listening
        """
        IOLoop.instance().stop()

    def shutdown(self):
        self.zmq_stream.on_recv(None)
        self.zmq_stream.socket.setsockopt(zmq.LINGER, 1000)
        self.zmq_stream.socket.close()
        self.zmq_stream.close()
        self.zmq_stream = None

        self.io_loop.remove_handler(self.incoming_dgram_sock.fileno())
        self.incoming_dgram_sock.close()
        self.incoming_dgram_sock = None
        os.remove(self.incoming_dgram_sock_sockfile)

        for s in self.out_streams:
            s.on_recv(None)
            s.socket.setsockopt(zmq.LINGER, 1000)
            s.socket.close()
            s.close()
        self.out_streams = {}
        self.out_stream_endpoints = {}

        # quit state and journal threads
        self.journal.quit()
        self.state.quit()


class StatsClient():
    def __init__(self, endpoint):
        self.context = Context()
        self.socket = self.context.socket(zmq.REQ)
        self.socket.connect(endpoint)

    def get(self, req_msg):
        self.socket.send_multipart([req_msg])
        message = self.socket.recv()
        return msgpack.unpackb(message)


class AsyncRm():
    def __init__(self):
        self._thread = threading.Thread(target=self.run)
        self._queue = Queue.Queue()
        self._should_run = True

    def run(self):
        while not self._queue.empty() or self._should_run:
            try:
                path, operation = self._queue.get(True, 1)
            except Queue.Empty:
                return
            if path is not None and path.startswith('/nfsmnt/'):
                path = os.path.join(RAMDISK_MOUNTPOINT, path.lstrip('/'))
                whiteout_path = path+'.whiteout'
                if os.path.isfile(whiteout_path):
                    os.unlink(whiteout_path)
                    logging.info('Removed whiteout file: %s' % path)

                if operation in TOUCH_OPS:
                    if os.path.isdir('/'.join(path.rstrip('/').split('/')[:-1])):
                        if os.path.isfile(path.rstrip('/')):
                            os.unlink(path.rstrip('/'))
                        open(path.rstrip('/'), 'a').close()
                        logging.info('Touched: %s' % path)
                    else:
                        logging.info('Parent not found for: %s' % path)
                else:
                    if os.path.isfile(path):
                        os.unlink(path)
                        logging.info('Removed file: %s' % path)
                    elif os.path.isdir(path):
                        subprocess.Popen(['rm', '-fr', path], stdout=subprocess.PIPE, stderr=subprocess.PIPE).communicate()
                        logging.info('Removed path: %s' % path)
                    else:
                        logging.info('Path not found: %s' % path)

    def put(self, record):
        self._queue.put(record)
        if self._should_run and not self._thread.isAlive():
            try:
                self._thread.start()
            except RuntimeError:
                self._thread = threading.Thread(target=self.run)
                self._thread.start()

    def quit(self):
        self._should_run = False
        self._queue = Queue.Queue()


class AsyncBase():
    WRITE_TYPE = 'a'

    def __init__(self, filename):
        self._filename = filename
        self._thread = threading.Thread(target=self.run)
        self._queue = Queue.Queue()
        self._should_run = True
        self._fd = None
        self.lines = 0  # for logrotate
        self.log_limit = 100000

    def run(self):
        while not self._queue.empty() or self._should_run:
            try:
                record = self._queue.get(True, 1)
            except Queue.Empty:
                return
            if record is not None:
                if not self._fd:
                    self._fd = open(self._filename, self.WRITE_TYPE)
                if self.WRITE_TYPE == 'w':
                    self._fd.seek(0)
                    self._fd.truncate()
                else:
                    self.lines += 1
                    if self.lines > self.log_limit:
                        self.rotate()
                self._fd.write(record)
                self._fd.flush()

    def rotate(self):
        if self._fd:
            self._fd.flush()
            self._fd.close()
            self._fd = None
        os.rename(self._filename, self._filename+'.1')
        self.lines = 0
        self._fd = open(self._filename, self.WRITE_TYPE)
        logging.info('Journal rotated')

    def put(self, record):
        self._queue.put(record)
        if self._should_run and not self._thread.isAlive():
            try:
                self._thread.start()
            except RuntimeError:
                self._thread = threading.Thread(target=self.run)
                self._thread.start()

    def quit(self):
        if self._fd:
            self._fd.close()
        self._should_run = False
        self._queue = Queue.Queue()


class AsyncJournalHandler(AsyncBase):
    WRITE_TYPE = 'a'

    def load_buffer(self, filename, backup_journal_dir, server):
        q = deque(maxlen=1000000)
        logging.info('Loading journal %s ...' % filename)
        max_timestamp = 0
        for fi, curr in [(filename+'.1', False), (filename, True)]:
            try:
                with open(fi, 'r') as f:
                    for line in f:
                        if curr:
                            self.lines += 1
                        try:
                            t, d = line.strip().split(' ', 1)
                            q.append((int(t), d))

                            curr_timestamp = float(d.split(' ', 1)[0])
                            if max_timestamp < d.split(' ', 1)[0]:
                                max_timestamp = curr_timestamp
                        except Exception as e:
                            pass
            except Exception as e:
                logging.error('Failed to load journal %s: %s' % (fi, e.message))
        logging.info('Loaded %i lines' % len(q))

        if backup_journal_dir is not None:
            c = 0
            try:
                for f in os.listdir(backup_journal_dir):
                    f = os.path.join(backup_journal_dir, f)
                    if os.path.isfile(f):
                        with open(f) as fd:
                            for l in fd:
                                try:
                                    l = l.strip()
                                    curr_timestamp = float(l.split(' ', 1)[0])
                                    if curr_timestamp > max_timestamp:
                                        invalidate, hit, operation, path = server.parse_msg(l)
                                        if invalidate:
                                            server.last_id += 1
                                            server.last_timestamp = time()
                                            q.append((server.last_id, l))
                                            server.journal.put("%i %s\n" % (server.last_id, l))
                                            c += 1
                                except Exception:
                                    pass
            except OSError:
                logging.error('Directory %s not found!' % backup_journal_dir)
            logging.info('Loaded %i lines (backup journal dir)' % c)

        return q


class AsyncState(AsyncBase):
    WRITE_TYPE = 'w'

    def __init__(self, filename):
        self.last = self.get(filename)
        AsyncBase.__init__(self, filename)

    @staticmethod
    def get(filename):
        try:
            with open(filename, 'r') as f:
                return json.load(f)
        except Exception as e:
            logging.error('Failed to load last state: %s' % e.message)
            return {}


class AsyncLogHandler(logging.handlers.WatchedFileHandler):
    def __init__(self, filename):
        logging.handlers.WatchedFileHandler.__init__(self, filename)
        self._thread = threading.Thread(target=self.run)
        self._queue = Queue.Queue()
        self._should_run = True

    def run(self):
        while not self._queue.empty() or self._should_run:
            try:
                record = self._queue.get(True, 1)
            except Queue.Empty:
                return
            if record is not None:
                logging.handlers.WatchedFileHandler.emit(self, record)

    def emit(self, record):
        self._queue.put(record)
        if self._should_run and not self._thread.isAlive():
            try:
                self._thread.start()
            except RuntimeError:
                self._thread = threading.Thread(target=self.run)
                self._thread.start()

    def quit(self):
        self._should_run = False
        self._queue.put(None)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Worker')
    parser.add_argument('--config', '-c', metavar='/etc/libiorouter/deloader.conf', type=str, default='/etc/libiorouter/deloader.conf', help='path to config file')
    parser.add_argument('--sockfile', '-s', metavar='/var/run/libiorouter.sock', type=str, help='socket for incoming data from libiorouter')
    parser.add_argument('--endpoint', '-e', metavar='tcp://127.0.0.1:3500', type=str, help='ip+port for all incoming connections')
    parser.add_argument('--targets', '-t', metavar='tcp://1.2.3.4:3500', type=str, nargs='+', help='target IPs and port ex. tcp://1.2.3.4:3500')
    parser.add_argument('--logfile', '-l', metavar='/var/log/libiorouter-deloader.log', type=str, help='logfile path')
    parser.add_argument('--state', '-a', metavar='/var/log/libiorouter-deloader.state', type=str, help='path to state file')
    parser.add_argument('--journal', '-j', metavar='/var/log/libiorouter-deloader.journal', type=str, help='path to journal file')
    parser.add_argument('--backup-journal-dir', '-J', metavar='/run/libiorouter/log/', type=str, help='path journal file from libiorouter')
    parser.add_argument('--get-state', '-g', metavar='tcp://1.2.3.4:3500', type=str, help='print state of worker')
    parser.add_argument('--get-cache-stats', '-G', metavar='tcp://1.2.3.4:3500', type=str, help='print cache stats of worker')
    args = parser.parse_args()

    if args.get_state or args.get_cache_stats:
        s = StatsClient(args.get_state or args.get_cache_stats)
        if args.get_state:
            print json.dumps(s.get(b'\x05'), indent=4)
        else:
            print 'count type  path\n================'
            for k, v in s.get(b'\x04'):
                print "%-5s %s" % (v, ' '.join(k))
        raise SystemExit

    # parse config file
    config = ConfigParser.SafeConfigParser()
    config.read(args.config)
    config_params = ['sockfile', 'endpoint', 'targets', 'logfile', 'state', 'journal', 'backup_journal_dir']
    settings = dict([(i, [] if i == 'targets' else None) for i in config_params])

    # parse config
    if config.has_section(CONFIG_SECTION):
        for n in config_params:
            if config.has_option(CONFIG_SECTION, n):
                v = config.get(CONFIG_SECTION, n)
                settings[n] = filter(None, v.split(' ')) if n == 'targets' else v

    # parse args
    for n in config_params:
        v = getattr(args, n)
        if v is not None:
            settings[n] = v

    # set defaults
    for n, v in [('state', '/var/log/libiorouter-deloader.state'), ('journal', '/var/log/libiorouter-deloader.journal')]:
        if settings[n] is None:
            settings[n] = v

    # initialize logfile
    log = logging.getLogger()  # root logger
    log.setLevel(logging.INFO)
    if settings['logfile']:
        log_handler = AsyncLogHandler(settings['logfile'])
    else:
        log_handler = logging.StreamHandler()
    log_handler.setFormatter(logging.Formatter('%(asctime)s %(levelname)s %(message)s'))
    log.addHandler(log_handler)  # set the new handler

    def log_except_hook(*exc_info):
        text = "".join(traceback.format_exception(*exc_info))
        logging.exception("Unhandled exception: %s", text)
    sys.excepthook = log_except_hook

    # print info config loading failed
    if not config.has_section(CONFIG_SECTION):
        logging.error('Failed to load config file')

    # check required params
    missing_params = [i for i in ['sockfile', 'endpoint', 'state', 'journal'] if settings[i] is None]
    if missing_params:
        logging.error('Parameters %s are not set.' % ', '.join(missing_params))
        raise SystemExit

    # init server
    s = Server(settings['sockfile'], settings['endpoint'], settings['targets'],
               settings['state'], settings['journal'], settings['backup_journal_dir'])

    # handle exit signals
    def handler(signum, frame):
        s.stop_listening()  # disabling main loop
    signal.signal(signal.SIGINT, handler)
    signal.signal(signal.SIGTERM, handler)

    logging.info('Running')
    s.start_listening()
    logging.info('Shutting down')
    s.shutdown()