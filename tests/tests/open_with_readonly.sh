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
cc -ggdb -o $TESTDIR/tests/bin/open_with_readonly $TESTDIR/tests/src/open_with_readonly.c

if [ ! -f $ROOTDIR/libiorouter.so ]; then 
	( cd $ROOTDIR && make)
fi
}

tearDown() {
	:;
	#rm -f $TESTDIR/tests/open_with_wronly $TESTDIR/tests/open_with_rdwr $TESTDIR/test/*.runstr
}

# test: Open with readonly access with IO routing on
# init: cache miss (is empty), no whiteouts
# expected behaviour:
# 1. check if file exist in $LIBIOR_CACHEDIR
# 2. open $TESTFILE
# 3. MUST create $CACHEFILE in $LIBIOR_CACHEDIR and copy contents from $TESTFILE 
# 4. return fd to $TESTFILE

test_open_with_readonly_io_on_miss() {
#local init
test_ts=`date +%s`
rm -f $CACHEFILE
rm -f $CACHEFILE.whiteout

#run
LIBIOR_IO=on strace -e open -o open_with_readonly_io.strace $TESTDIR/tests/bin/open_with_readonly $TESTFILE
opened_file=`awk -F\" 'END{print $2}' open_with_readonly_io.strace`
#test
assertFalse "$TESTFILE whiteout MUST NOT exist in `dirname $CACHEFILE`" "[ -f $CACHEFILE.whiteout ]"

assertTrue "$TESTFILE MUST exist in `dirname $CACHEFILE`" "[ -f $CASHEFILE ]" || exit $?
cache_md5sum=`md5sum $CACHEFILE`
cache_md5sum=${cache_md5sum% *}
testfile_md5sum=`md5sum $TESTFILE`
testfile_md5sum=${testfile_md5sum% *}
assertEquals "$TESTFILE MUST have same md5sum as a $CACHEFILE" "$testfile_md5sum" "$cache_md5sum"
assertEquals "Wrong opened file: " "$TESTFILE" "$opened_file"

file_ts=`stat -c %Z $CACHEFILE`
assertTrue "$CACHEFILE has to be newer than timestamp of test start (`date -d@$test_ts`)" "[ "$file_ts" -ge "$test_ts" ]"

#debug
local stracestr="LIBIOR_IO=on LIBIOR_REWRITEDIR=$LIBIOR_REWRITEDIR LIBIOR_CACHEDIR=$LIBIOR_CACHEDIR LD_PRELOAD=$LD_PRELOAD $RUNSTR strace -s 256 $TESTDIR/tests/open_with_readonly $TESTFILE"
echo $stracestr >  $TESTDIR/tests/bin/open_with_readonly.runstr
}

# test: Open with readonly access with IO routing on
# init: cache hit,no whiteouts
# expected behaviour:
# 1. check if file exist in $LIBIOR_CACHEDIR
# 2. open $CACHEFILE
# 4. return fd to $CACHEFILE

test_open_with_readonly_io_on_hit() {
#local init

#run
LIBIOR_IO=on strace -e open -o open_with_readonly_io.strace $TESTDIR/tests/bin/open_with_readonly $TESTFILE
opened_file=`awk -F\" 'END{print $2}' open_with_readonly_io.strace`
rm open_with_readonly_io.strace

assertTrue "$TESTFILE MUST exist in `dirname $CACHEFILE`" "[ -f $CACHEFILE ]"
assertEquals "$TESTFILE MUST have same md5sum as a $CACHEFILE" "$testfile_md5sum" "$cache_md5sum"

assertEquals "Wrongly opened file: " "$CACHEFILE" "$opened_file"

#debug
local stracestr="LIBIOR_IO=on LIBIOR_REWRITEDIR=$LIBIOR_REWRITEDIR LIBIOR_CACHEDIR=$LIBIOR_CACHEDIR LD_PRELOAD=$LD_PRELOAD $RUNSTR strace -s 256 $TESTDIR/tests/open_with_readonly $TESTFILE"
echo $stracestr >  $TESTDIR/tests/logs/open_with_readonly.runstr
}

source "/usr/share/shunit2/shunit2"
