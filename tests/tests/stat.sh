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
cc -ggdb -o $TESTDIR/tests/stat $TESTDIR/tests/src/stat.c

if [ ! -f $ROOTDIR/libiorouter.so ]; then 
	( cd $ROOTDIR && make)
fi
}

tearDown() {
	:;
	#rm -f $TESTDIR/tests/open_with_wronly $TESTDIR/tests/open_with_rdwr $TESTDIR/test/*.runstr
}

# test: stat file on MISS,IO on
# init: cache miss (is empty), no whiteouts
# expected behaviour:
# 1. check if file exist in $LIBIOR_CACHEDIR (it does not)
# 2. stat $TESTFILE
# 3. copy $TESTFILE to $LIBIOR_CACHEDIR

test_stat_io_on_miss() {
#local init
test_ts=`date +%s`
rm -f $CACHEFILE
rm -f $CACHEFILE.whiteout

#run
LIBIOR_IO=on strace -e stat -o stat_io.strace $TESTDIR/tests/stat $TESTFILE
stated_file=`awk -F\" 'END{print $2}' stat_io.strace`
#test

assertTrue "$TESTFILE MUST exist in `dirname $CACHEFILE`" "[ -f $CASHEFILE ]" || exit $?
cache_md5sum=`md5sum $CACHEFILE`
cache_md5sum=${cache_md5sum% *}
testfile_md5sum=`md5sum $TESTFILE`
testfile_md5sum=${testfile_md5sum% *}
assertEquals "$TESTFILE MUST have same md5sum as a $CACHEFILE" "$testfile_md5sum" "$cache_md5sum"
assertEquals "Wrong stat()ed file: " "$TESTFILE" "$stated_file"

file_ts=`stat -c %Z $CACHEFILE`
assertTrue "$CACHEFILE has to be newer than timestamp of test start (`date -d@$test_ts`)" "[ "$file_ts" -ge "$test_ts" ]"

#debug
local stracestr="LIBIOR_IO=on LIBIOR_REWRITEDIR=$LIBIOR_REWRITEDIR LIBIOR_CACHEDIR=$LIBIOR_CACHEDIR LD_PRELOAD=$LD_PRELOAD $RUNSTR strace -s 256 $TESTDIR/tests/stat $TESTFILE"
echo $stracestr >  $TESTDIR/tests/stat.runstr
}

# test: stat file on MISS,IO off
# init: cache miss (is empty), no whiteouts
# expected behaviour:
# 1. check if file exist in $LIBIOR_CACHEDIR (it does not)
# 2. stat $TESTFILE
# 3. copy $TESTFILE to $LIBIOR_CACHEDIR

test_stat_io_off_miss() {
#local init
test_ts=`date +%s`

#run
LIBIOR_IO=off strace -e stat -o stat_io.strace $TESTDIR/tests/stat $TESTFILE
stated_file=`awk -F\" 'END{print $2}' stat_io.strace`
#test

assertEquals "Wrong stat()ed file: " "$TESTFILE" "$stated_file"

}

# test: stat file on HIT, IO on
# init: cache is hot
# expected behaviour:
# 1. check if file exist in $LIBIOR_CACHEDIR (it does not)
# 2. stat $CACHEFILE

test_stat_io_on_hit() {
#local init

#run
LIBIOR_IO=on strace -e stat -o stat_io.strace $TESTDIR/tests/stat $TESTFILE
stated_file=`awk -F\" 'END{print $2}' stat_io.strace`
#test

assertTrue "$TESTFILE MUST exist in `dirname $CACHEFILE`" "[ -f $CASHEFILE ]" || exit $?
cache_md5sum=`md5sum $CACHEFILE`
cache_md5sum=${cache_md5sum% *}
testfile_md5sum=`md5sum $TESTFILE`
testfile_md5sum=${testfile_md5sum% *}
assertEquals "$TESTFILE MUST have same md5sum as a $CACHEFILE" "$testfile_md5sum" "$cache_md5sum"
assertEquals "Wrong stat()ed file: " "$CACHEFILE" "$stated_file"

#debug
local stracestr="LIBIOR_IO=on LIBIOR_REWRITEDIR=$LIBIOR_REWRITEDIR LIBIOR_CACHEDIR=$LIBIOR_CACHEDIR LD_PRELOAD=$LD_PRELOAD $RUNSTR strace -s 256 $TESTDIR/tests/stat $TESTFILE"
echo $stracestr >  $TESTDIR/tests/stat.runstr
}

# test: stat file on HIT,IO off
# init: cache is hot,no whiteouts
# expected behaviour:
# 1. stat $TESTFILE
# 2. return stat buffer for $TESTFILE

test_stat_io_off_hit() {
#local init
touch $CACHEFILE

#run
LIBIOR_IO=off strace -e stat -o stat_io.strace $TESTDIR/tests/stat $TESTFILE
stated_file=`awk -F\" 'END{print $2}' stat_io.strace`

#test

assertEquals "Wrong stat()ed file: " "$TESTFILE" "$stated_file"
local stracestr="LIBIOR_IO=off LIBIOR_REWRITEDIR=$LIBIOR_REWRITEDIR LIBIOR_CACHEDIR=$LIBIOR_CACHEDIR LD_PRELOAD=$LD_PRELOAD $RUNSTR strace -s 256 $TESTDIR/tests/stat $TESTFILE"
echo $stracestr >  $TESTDIR/tests/stat.runstr
}

source "/usr/share/shunit2/shunit2"
