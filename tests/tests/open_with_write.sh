# Test if file is deleted from caching directory during IO routing ON
#!/bin/bash
ROOTDIR=${PWD%*/*/*}
TESTDIR=$ROOTDIR/tests
TESTFILE=$TESTDIR/run$TESTDIR/nfsmnt/test.php

setUp() {
if [ ! -f $TESTDIR/nfsmnt/test.php ]; then
	dd if=/dev/urandom of=$TESTDIR/nfsmnt/test.php count=10
fi
cc -ggdb -o $TESTDIR/tests/open_with_write $TESTDIR/tests/src/open_with_write.c

if [ ! -f $ROOTDIR/libiorouter.so ]; then 
	( cd $ROOTDIR && make)
fi
}

tearDown() {
	:;
	#echo rm -f $TESTDIR/tests/open_with_write
}


test_open_with_write_io_on() {
test_ts=`date +%s`
LIBIOR_IO=on LIBIOR_REWRITEDIR=$TESTDIR/nfsmnt LIBIOR_CACHEDIR=$TESTDIR/run LD_PRELOAD=$ROOTDIR/libiorouter.so $TESTDIR/tests/open_with_write $TESTDIR/nfsmnt/test.php
echo LIBIOR_IO=on LIBIOR_REWRITEDIR=$TESTDIR/nfsmnt LIBIOR_CACHEDIR=$TESTDIR/run LD_PRELOAD=$ROOTDIR/libiorouter.so strace -s 256 $TESTDIR/tests/open_with_write $TESTDIR/nfsmnt/test.php > $TESTDIR/tests/open_with_write_io_on.runstr

cat << EOF >> $TESTDIR/tests/open_with_write_io_on.runstr
set environment LIBIOR_IO=on
set environment LIBIOR_REWRITEDIR=$TESTDIR/nfsmnt
set environment LIBIOR_CACHEDIR=$TESTDIR/run
set environment LD_PRELOAD=$ROOTDIR/libiorouter.so
EOF
assertTrue "$TESTDIR/nfsmnt/test.php MUST exist in $TESTDIR/run/$TESTIDR" "[ -f $TESTFILE ]"
assertFalse "$TESTDIR/nfsmnt/test.php MUST have zero size in $TESTFILE" "[ -s $TESTFILE ]"
file_ts=`stat -c %Z $TESTFILE`
assertTrue "$TESTFILE has to be newer than timestamp of test start (`date -d@$test_ts`)" "[ "$file_ts" -ge "$test_ts" ]"
}

test_open_with_write_io_off() {
LIBIOR_IO=off LIBIOR_REWRITEDIR=$TESTDIR/nfsmnt LIBIOR_CACHEDIR=$TESTDIR/run LD_PRELOAD=$ROOTDIR/libiorouter.so $TESTDIR/tests/open_with_write $TESTDIR/nfsmnt/test.php
echo LIBIOR_IO=off LIBIOR_REWRITEDIR=$TESTDIR/nfsmnt LIBIOR_CACHEDIR=$TESTDIR/run LD_PRELOAD=$ROOTDIR/libiorouter.so strace -s 256 $TESTDIR/tests/open_with_write $TESTDIR/nfsmnt/test.php > $TESTDIR/tests/open_with_write_io_off.runstr

cat << EOF >> $TESTDIR/tests/open_with_write_io_off.runstr
set environment LIBIOR_IO=off
set environment LIBIOR_REWRITEDIR=$TESTDIR/nfsmnt
set environment LIBIOR_CACHEDIR=$TESTDIR/run
set environment LD_PRELOAD=$ROOTDIR/libiorouter.so
EOF

assertFalse "$TESTDIR/nfsmnt/test.php MUST NOT exist in ${TESTFILE:h}" "[ -f $TESTFILE ]"
}


source "/usr/share/shunit2/shunit2"
