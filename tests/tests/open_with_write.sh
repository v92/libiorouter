# Test if file is deleted from caching directory during IO routing ON
#!/bin/bash
ROOTDIR=${PWD%*/*/*}
TESTDIR=$ROOTDIR/tests
CACHEFILE=$TESTDIR/run/${TESTDIR:1}/nfsmnt/test.php
TESTFILE=$TESTDIR/nfsmnt/test.php
LIBIOR_REWRITEDIR=$TESTDIR/nfsmnt
LIBIOR_CACHEDIR=$TESTDIR/run
LD_PRELOAD=$ROOTDIR/libiorouter.so

setUp() {
if [ ! -f $TESTDIR/nfsmnt/test.php ]; then
	dd if=/dev/urandom of=$TESTDIR/nfsmnt/test.php count=10
fi
cc -ggdb -o $TESTDIR/tests/open_with_wronly $TESTDIR/tests/src/open_with_wronly.c
cc -ggdb -o $TESTDIR/tests/open_with_rdwr $TESTDIR/tests/src/open_with_rdwr.c

if [ ! -f $ROOTDIR/libiorouter.so ]; then 
	( cd $ROOTDIR && make)
fi
}

tearDown() {
	:;
	#echo rm -f $TESTDIR/tests/open_with_write
}


test_open_with_rdwr_io_on() {
#local init
test_ts=`date +%s`

#run
LIBIOR_IO=on $TESTDIR/tests/open_with_rdwr $TESTFILE

#test
assertTrue "$TESTFILE MUST exist in `dirname $CACHEFILE`" "[ -f $CACHEFILE ]"
assertFalse "$TESTFILE MUST have zero size as a $CACHEFILE" "[ -s $CACHEFILE ]"
file_ts=`stat -c %Z $CACHEFILE`

#debug
assertTrue "$CACHEFILE has to be newer than timestamp of test start (`date -d@$test_ts`)" "[ "$file_ts" -ge "$test_ts" ]"

local stracestr="LIBIOR_IO=on $RUNSTR strace -s 256 $TESTDIR/tests/open_with_rdwr $TESTFILE $TESTDIR/tests/open_with_rdwe_io_on.runstr"
}

test_open_with_wronly_io_on() {
#local init
test_ts=`date +%s`

#run
LIBIOR_IO=on $TESTDIR/tests/open_with_wronly $TESTFILE

#test
assertTrue "$TESTFILE MUST exist in `dirname $CACHEFILE`" "[ -f $CACHEFILE ]"
assertFalse "$TESTFILE MUST have zero size in $CACHEFILE" "[ -s $CACHEFILE ]"
file_ts=`stat -c %Z $CACHEFILE`

#debug
assertTrue "$CACHEFILE has to be newer than timestamp of test start (`date -d@$test_ts`)" "[ "$file_ts" -ge "$test_ts" ]"

local stracestr="LIBIOR_IO=on $RUNSTR strace -s 256 $TESTDIR/tests/open_with_wronly $TESTFILE $TESTDIR/tests/open_with_wronly_io_on.runstr"
}

test_open_with_wronly_io_off() {
#local init

#run
LIBIOR_IO=off $TESTDIR/tests/open_with_wronly $TESTFILE

#test
assertFalse "$TESTFILE MUST not exist in `dirname $CACHEFILE`" "[ -f $CACHEFILE ]"

#debug
local stracestr="LIBIOR_IO=on $RUNSTR strace -s 256 $TESTDIR/tests/open_with_wronly $TESTFILE $TESTDIR/tests/open_with_wronly_io_on.runstr"
}

source "/usr/share/shunit2/shunit2"
