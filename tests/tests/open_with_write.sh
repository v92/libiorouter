#!/bin/bash
ROOTDIR=${PWD%*/*/*}
TESTDIR=$ROOTDIR/tests
CACHEFILE=$TESTDIR/run/${TESTDIR:1}/nfsmnt/test.php
TESTFILE=$TESTDIR/nfsmnt/test.php
export LIBIOR_REWRITEDIR=$TESTDIR/nfsmnt
export LIBIOR_CACHEDIR=$TESTDIR/run
export LD_PRELOAD=$ROOTDIR/libiorouter.so

setUp() {
if [ ! -f $TESTDIR/nfsmnt/test.php ]; then
	dd if=/dev/urandom of=$TESTDIR/nfsmnt/test.php count=10
fi
test -d $TESTDIR/tests/bin || mkdir $TESTDIR/tests/bin
test -d $TESTDIR/tests/logs || mkdir $TESTDIR/tests/logs
cc -ggdb -o $TESTDIR/tests/bin/open_with_wronly $TESTDIR/tests/src/open_with_wronly.c
cc -ggdb -o $TESTDIR/tests/bin/open_with_rdwr $TESTDIR/tests/src/open_with_rdwr.c
cc -ggdb -o $TESTDIR/tests/bin/open_with_append $TESTDIR/tests/src/open_with_append.c
cc -ggdb -o $TESTDIR/tests/bin/open_with_create $TESTDIR/tests/src/open_with_create.c
cc -ggdb -o $TESTDIR/tests/bin/open_with_trunc $TESTDIR/tests/src/open_with_trunc.c

if [ ! -f $ROOTDIR/libiorouter.so ]; then 
	( cd $ROOTDIR && make)
fi
touch $CACHEFILE
touch $CACHEFILE.whiteout
}

tearDown() {
	:;
	#rm -f $TESTDIR/tests/open_with_wronly $TESTDIR/tests/open_with_rdwr $TESTDIR/test/*.runstr
}

do_test_with_params() {
local fn=$1
local io=$2
if [[ -z "$fn" || -z "$io" ]]; then
	fail "Wront parameters for do_test_with_params($fn,$io)"
	return 1
fi
#run
LIBIOR_IO=$io $TESTDIR/tests/open_with_$fn $TESTFILE

#test
assertFalse "$TESTFILE MUST NOT exist in `dirname $CACHEFILE`" "[ -f $CACHEFILE ]"
assertFalse "$TESTFILE whiteout MUST NOT exist in `dirname $CACHEFILE`" "[ -f $CACHEFILE.whiteout ]"

echo "LIBIOR_IO=on LIBIOR_REWRITEDIR=$LIBIOR_REWRITEDIR LIBIOR_CACHEDIR=$LIBIOR_CACHEDIR LD_PRELOAD=$LD_PRELOAD $RUNSTR strace -s 256 $TESTDIR/tests/open_with_$fn $TESTFILE" > $TESTDIR/tests/logs/open_with_${fn}_io_${io}.runstr
}

# test: Open with write/append/trunc/creat flags with IO routing on or off
# expected behaviour:
# 1. MUST delete old $CACHEFILE and $CACHEFILE.whiteout from $LIBIOR_CACHEDIR
# 2. open $TESTFILE
# 3. return fd to $TESTFILE
# No files has to be generated in $LIBIOR_CACHEDIR

test_open_with_rdwr_io_on() {
	do_test_with_params "rdwr" "on"

}

test_open_with_wronly_io_on() {
	do_test_with_params "wronly" "on"
}

test_open_with_wronly_io_off() {
	do_test_with_params "wronly" "off"
}

test_open_with_append_io_on() {
	do_test_with_params "append" "on"
}

test_open_with_append_io_off() {
	do_test_with_params "append" "off"
}

test_open_with_create_io_on() {
	do_test_with_params "create" "on"
}

test_open_with_create_io_off() {
	do_test_with_params "create" "off"
}

test_open_with_trunc_io_on() {
	do_test_with_params "trunc" "on"
}

test_open_with_trunc_io_off() {
	do_test_with_params "trunc" "off"
}

source "/usr/share/shunit2/shunit2"
