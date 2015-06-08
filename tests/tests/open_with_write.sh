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
cc -ggdb -o $TESTDIR/tests/open_with_wronly $TESTDIR/tests/src/open_with_wronly.c
cc -ggdb -o $TESTDIR/tests/open_with_rdwr $TESTDIR/tests/src/open_with_rdwr.c
cc -ggdb -o $TESTDIR/tests/open_with_append $TESTDIR/tests/src/open_with_append.c

if [ ! -f $ROOTDIR/libiorouter.so ]; then 
	( cd $ROOTDIR && make)
fi
}

tearDown() {
	:;
	#rm -f $TESTDIR/tests/open_with_wronly $TESTDIR/tests/open_with_rdwr $TESTDIR/test/*.runstr
}

# test: Open with readwrite access with IO routing on
# expected behaviour:
# 1. MUST delete old $CACHEFILE and $CACHEFILE.whiteout from $LIBIOR_CACHEDIR
# 2. open $TESTFILE
# 3. SHOULD create $CACHEFILE in $LIBIOR_CACHEDIR and copy contents from $TESTFILE 
# 4. return fd to $TESTFILE

test_open_with_rdwr_io_on() {
#local init
test_ts=`date +%s`
touch $CACHEFILE
touch $CACHEFILE.whiteout

#run
LIBIOR_IO=on $TESTDIR/tests/open_with_rdwr $TESTFILE

#test
assertFalse "$TESTFILE MUST NOT exist in `dirname $CACHEFILE`" "[ -f $CACHEFILE ]"
assertFalse "$TESTFILE whiteout MUST NOT exist in `dirname $CACHEFILE`" "[ -f $CACHEFILE.whiteout ]"

#cache_md5sum=`md5sum $CACHEFILE`
#cache_md5sum=${cache_md5sum% *}
#testfile_md5sum=`md5sum $TESTFILE`
#testfile_md5sum=${testfile_md5sum% *}
#assertEquals "$TESTFILE MUST have same md5sum as a $CACHEFILE" "$testfile_md5sum" "$cache_md5sum"

#file_ts=`stat -c %Z $CACHEFILE`
#assertTrue "$CACHEFILE has to be newer than timestamp of test start (`date -d@$test_ts`)" "[ "$file_ts" -ge "$test_ts" ]"

#debug
local stracestr="LIBIOR_IO=on LIBIOR_REWRITEDIR=$LIBIOR_REWRITEDIR LIBIOR_CACHEDIR=$LIBIOR_CACHEDIR LD_PRELOAD=$LD_PRELOAD $RUNSTR strace -s 256 $TESTDIR/tests/open_with_rdwr $TESTFILE"
echo $stracestr >  $TESTDIR/tests/open_with_rdwr_io_on.runstr
}

# Test: Open with writeonly access with IO routing on
# Expected behaviour:
# 1. MUST delete old $CACHEFILE and $CACHEFILE.whiteout from $LIBIOR_CACHEDIR
# 2. open $TESTFILE
# 3. return fd to $TESTFILE
# No files has to be generated in $LIBIOR_CACHEDIR

test_open_with_wronly_io_on() {
#local init
test_ts=`date +%s`
touch $CACHEFILE
touch $CACHEFILE.whiteout

#run
LIBIOR_IO=on $TESTDIR/tests/open_with_wronly $TESTFILE

echo $stracestr > $TESTDIR/tests/open_with_wronly_io_on.runstr
#test
assertFalse "$TESTFILE MUST NOT exist in `dirname $CACHEFILE`" "[ -f $CACHEFILE ]"
local stracestr="LIBIOR_IO=on LIBIOR_REWRITEDIR=$LIBIOR_REWRITEDIR LIBIOR_CACHEDIR=$LIBIOR_CACHEDIR LD_PRELOAD=$LD_PRELOAD $RUNSTR strace -s 256 $TESTDIR/tests/open_with_wronly $TESTFILE"
assertFalse "$TESTFILE whiteout MUST NOT exist in `dirname $CACHEFILE`" "[ -f $CACHEFILE.whiteout ]"

#debug
local stracestr="LIBIOR_IO=on LIBIOR_REWRITEDIR=$LIBIOR_REWRITEDIR LIBIOR_CACHEDIR=$LIBIOR_CACHEDIR LD_PRELOAD=$LD_PRELOAD $RUNSTR strace -s 256 $TESTDIR/tests/open_with_wronly $TESTFILE"
}

# Test: Open with writeonly access with IO routing off
# Expected behaviour:
# 1. MUST delete old $CACHEFILE and $CACHEFILE.whiteout from $LIBIOR_CACHEDIR
# 2. open $TESTFILE
# 3. return fd to $TESTFILE

test_open_with_wronly_io_off() {
#local init
touch $CACHEFILE
touch $CACHEFILE.whiteout

#run
LIBIOR_IO=off $TESTDIR/tests/open_with_wronly $TESTFILE

#test
assertFalse "$TESTFILE MUST not exist in `dirname $CACHEFILE`" "[ -f $CACHEFILE ]"
assertFalse "$TESTFILE whiteout MUST NOT exist in `dirname $CACHEFILE`" "[ -f $CACHEFILE.whiteout ]"

#debug
local stracestr="LIBIOR_IO=on $RUNSTR strace -s 256 $TESTDIR/tests/open_with_wronly $TESTFILE $TESTDIR/tests/open_with_wronly_io_on.runstr"
}

# Test: Open with O_APPEND with IO routing ON and OFF
# Expected behaviour:
# 1. MUST delete old $CACHEFILE and $CACHEFILE.whiteout from $LIBIOR_CACHEDIR
# 2. open $TESTFILE 
# 3. return fd to $TESTFILE

test_open_with_append_io_on() {
#local init
touch $CACHEFILE
touch $CACHEFILE.whiteout

#run
LIBIOR_IO=on $TESTDIR/tests/open_with_append $TESTFILE

#test
assertFalse "$TESTFILE MUST not exist in `dirname $CACHEFILE`" "[ -f $CACHEFILE ]"
assertFalse "$TESTFILE whiteout MUST NOT exist in `dirname $CACHEFILE`" "[ -f $CACHEFILE.whiteout ]"

#debug
local stracestr="LIBIOR_IO=on $RUNSTR strace -s 256 $TESTDIR/tests/open_with_append $TESTFILE $TESTDIR/tests/open_with_append_io_on.runstr"
}

test_open_with_append_io_off() {
#local init
touch $CACHEFILE
touch $CACHEFILE.whiteout

#run
LIBIOR_IO=off $TESTDIR/tests/open_with_append $TESTFILE

#test
assertFalse "$TESTFILE MUST not exist in `dirname $CACHEFILE`" "[ -f $CACHEFILE ]"
assertFalse "$TESTFILE whiteout MUST NOT exist in `dirname $CACHEFILE`" "[ -f $CACHEFILE.whiteout ]"

#debug
local stracestr="LIBIOR_IO=on LIBIOR_REWRITEDIR=$LIBIOR_REWRITEDIR LIBIOR_CACHEDIR=$LIBIOR_CACHEDIR LD_PRELOAD=$LD_PRELOAD $RUNSTR strace -s 256 $TESTDIR/tests/open_with_append $TESTFILE"
echo $stracestr >  $TESTDIR/tests/open_with_append_io_off.runstr
}

source "/usr/share/shunit2/shunit2"
