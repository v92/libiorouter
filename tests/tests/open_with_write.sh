#!/bin/bash
ROOTDIR=${PWD%*/*/*}
TESTDIR=$ROOTDIR/tests

setUp() {
if [ ! -f $TESTDIR/nfsmnt/test.php ]; then
	dd if=/dev/urandom of=$TESTDIR/nfsmnt/test.php count=10
fi
cc -ggdb -o $TESTDIR/tests/open_with_write open_with_write.c

if [ ! -f $ROOTDIR/libiorouter.so ]; then 
	( cd $ROOTDIR && make)
fi
}

tearDown() {
	:;
	#echo rm -f $TESTDIR/tests/open_with_write
}


test_open_with_write() {
LIBIOR_IO=on LIBIOR_REWRITEDIR=$TESTDIR/nfsmnt LIBIOR_CACHEDIR=$TESTDIR/run LD_PRELOAD=$ROOTDIR/libiorouter.so $TESTDIR/tests/open_with_write $TESTDIR/nfsmnt/test.php
echo LIBIOR_IO=on LIBIOR_REWRITEDIR=$TESTDIR/nfsmnt LIBIOR_CACHEDIR=$TESTDIR/run LD_PRELOAD=$ROOTDIR/libiorouter.so strace -s 256 $TESTDIR/tests/open_with_write $TESTDIR/nfsmnt/test.php > $TESTDIR/tests/open_with_write.runstr

cat << EOF >> $TESTDIR/tests/open_with_write.runstr
set environment LIBIOR_IO=on
set environment LIBIOR_REWRITEDIR=$TESTDIR/nfsmnt
set environment LIBIOR_CACHEDIR=$TESTDIR/run
set environment LD_PRELOAD=$ROOTDIR/libiorouter.so
EOF

assertFalse "$TESTDIR/nfsmnt/test.php MUST NOT exist in $TESTDIR/run: " "[ -f $TESTDIR/nfsmnt/test.php ]"
}

source "/usr/share/shunit2/shunit2"
