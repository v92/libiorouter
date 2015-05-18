<a href="https://scan.coverity.com/projects/4222">
<img alt="Coverity Scan Build Status"
src="https://scan.coverity.com/projects/4222/badge.svg"/>
</a>
[![Build Status](https://travis-ci.org/v92/libiorouter.svg?branch=master)](https://travis-ci.org/v92/libiorouter)
# libiorouter
libiorouter is LD_PRELOAD library which gives application better control of caching backend filesystem and cache invalidation. It works transparently by hooking IO calls and redirect them to LIBIOR_CACHEDIR which is usually tmpfs (but any other FS can be used).

## Motivation
It was created because we were struggling with attribute caching on NFS shares which were used for serving PHP hosting. PHP is very metadata hungry because it generate tremendous number of stat()/access() calls. Those calls are usually same or redundant,so there is no point to ask them again from backend directory. By increasing attribute cache, we also significantly affected cache coherency between NFS clients. Decreasing was out of questions because increased latency would slow down hosting too much. So we decided to use NFS share without attribute caching and leave it to libiorouter.

A lot of web hosting data won't change much over time. We really needed to handle only small subset of those data which change occasionally (through FTP).

It also provides ability to log every single IO call with minimum overhead to LIBIOR_CACHEDIR/iostats/<pid>. Those IO calls which changes data or metadata on backend filesystems (and thus cachedir) are sent to localhost UDP port 12345 for further processing. 

libiorouter does not provide mechanism for cache invalidation between NFS clients, but it will invalidate cache on same host. It is on user to implement cache invalidation between NFS clients which suits its needs by using daemon which listens on port 12345.

# Building and usage

Building is done with simple make in source directory. 
```
~/libiorouter$ make
gcc libiorouter.c iohooks.c normalize_path.c -ansi -Wall -g -ldl -shared -fPIC -o libiorouter.so
```

Usage of libiorouter is simple :
```
root@host:~# LD_PRELOAD=/path/to/libiorouter.so app
```

Or you may put it into /etc/ld.so.preload.

# Controling libiorouter
libiorouter can be controled in two ways:
* by setting environment variables 
* signals

Environment with their defauls are as follows:
* LIBIOR_IO = (yes|no) [no] - turns caching on or off. It won't cache but it will still try to execute IO operations that changes metadata (chown,chmod,rmdir,unlink) in LIBIOR_CACHEDIR .
* LIBIOR_CACHEDIR - [/run] - sets directory where backend directory is going to be shadowed / cached .
* LIBIOR_REWRITEDIR - [/nfsmnt] - sets backend directory which will be shadowed / cached to LIBIOR_CACHEDIR .
* LIBIOR_MAXFILESIZE - [10MB] - sets maximum file size which will be cached in LIBIOR_CACHEDIR. Value is in bytes.
* LIBIOR_WHITELIST_REGEX - list of apps for which is IO routing automatically enabled. Here are applications that were tested with libiorouter and are known to work. You may change defaults in libiorouter.h . LIBIOR_IO overwrites this setting.

Signals:
* SIGPROF - close()/open() log file for rotating purposes
* SIGURG - turns IO on/off
* SIGTTOU - turn debug messages on/off
* SIGTTIN - turn traceing (stats) on/off

# Testing

libiorouter successfuly passes cthon04 test suite. We also plan add libiorouter own test suite.

# TODO
* CI testing suite
* debian package
