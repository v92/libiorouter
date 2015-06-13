#!/bin/bash
source init.sh

setUp() {
if [ ! -f $TESTDIR/nfsmnt/test.php ]; then
	dd if=/dev/urandom of=$TESTDIR/nfsmnt/test.php count=10
fi
test -d $TESTDIR/tests/bin || mkdir $TESTDIR/tests/bin
test -d $TESTDIR/tests/logs || mkdir $TESTDIR/tests/logs
cc -ggdb -o $TESTDIR/tests/bin/stat $TESTDIR/tests/src/stat.c

if [ ! -f $ROOTDIR/libiorouter.so ]; then 
	( cd $ROOTDIR && make)
fi
export testfile_size=`stat -c %s $TESTFILE`
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
LIBIOR_IO=on strace -e stat -o stat_io.strace $TESTDIR/tests/bin/stat $TESTFILE
stated_file=`awk -F\" 'END{print $2}' stat_io.strace`
#test

assertTrue "$TESTFILE MUST exist in `dirname $CACHEFILE`" "[ -f $CASHEFILE ]" || exit $?
if [ "$LIBIOR_MAXFILESIZE" -gt "$testfile_size" ]; then 
	cache_md5sum=`md5sum $CACHEFILE`
	cache_md5sum=${cache_md5sum% *}
	testfile_md5sum=`md5sum $TESTFILE`
	testfile_md5sum=${testfile_md5sum% *}
	assertEquals "$TESTFILE MUST have same md5sum as a $CACHEFILE" "$testfile_md5sum" "$cache_md5sum"
else
	cachefile_size=`stat -c %s $CACHEFILE`
	assertEquals "$CACHEFILE MUST have zero size if $TESTFILE is bigger than LIBIOR_MAXFILESIZE ($LIBIOR_MAXFILESIZE):" "0" "$cachefile_size"
fi
assertEquals "Wrong stat()ed file: " "$TESTFILE" "$stated_file"

file_ts=`stat -c %Z $CACHEFILE`
assertTrue "$CACHEFILE has to be newer than timestamp of test start (`date -d@$test_ts`)" "[ "$file_ts" -ge "$test_ts" ]"

#debug
local stracestr="LIBIOR_IO=on LIBIOR_REWRITEDIR=$LIBIOR_REWRITEDIR LIBIOR_CACHEDIR=$LIBIOR_CACHEDIR LD_PRELOAD=$LD_PRELOAD $RUNSTR strace -s 256 $TESTDIR/tests/bin/stat $TESTFILE"
echo $stracestr >  $TESTDIR/tests/logs/stat.runstr
}

# test: stat file on MISS,IO off
# init: cache miss (is empty), no whiteouts
# expected behaviour:
# 1. stat $TESTFILE
# 2. return stat buffer for $TESTFILE

test_stat_io_off_miss() {
#local init
test_ts=`date +%s`

#run
LIBIOR_IO=off strace -e stat -o stat_io.strace $TESTDIR/tests/bin/stat $TESTFILE
stated_file=`awk -F\" 'END{print $2}' stat_io.strace`
#test

assertEquals "Wrong stat()ed file: " "$TESTFILE" "$stated_file"

}

# test: stat file on HIT, IO on
# init: cache is hot
# expected behaviour:
# 1. stat $CACHEFILE
# 2. return stat buffer for $CACHEFILE


test_stat_io_on_hit() {
#local init

#run
LIBIOR_IO=on strace -e stat -o stat_io.strace $TESTDIR/tests/bin/stat $TESTFILE
stated_file=`awk -F\" 'END{print $2}' stat_io.strace`
#test

assertTrue "$TESTFILE MUST exist in `dirname $CACHEFILE`" "[ -f $CASHEFILE ]" || exit $?
if [ "$LIBIOR_MAXFILESIZE" -gt "$testfile_size" ]; then 
	cache_md5sum=`md5sum $CACHEFILE`
	cache_md5sum=${cache_md5sum% *}
	testfile_md5sum=`md5sum $TESTFILE`
	testfile_md5sum=${testfile_md5sum% *}
	assertEquals "$TESTFILE MUST have same md5sum as a $CACHEFILE" "$testfile_md5sum" "$cache_md5sum"
	assertEquals "Wrong stat()ed file: " "$CACHEFILE" "$stated_file"
else
	cachefile_size=`stat -c %s $CACHEFILE`
	assertEquals "$CACHEFILE MUST have zero size if $TESTFILE is bigger than LIBIOR_MAXFILESIZE ($LIBIOR_MAXFILESIZE):" "0" "$cachefile_size"
	assertEquals "Wrong stat()ed file: " "$TESTFILE" "$stated_file"
fi

#debug
local stracestr="LIBIOR_IO=on LIBIOR_REWRITEDIR=$LIBIOR_REWRITEDIR LIBIOR_CACHEDIR=$LIBIOR_CACHEDIR LD_PRELOAD=$LD_PRELOAD $RUNSTR strace -s 256 $TESTDIR/tests/bin/stat $TESTFILE"
echo $stracestr >  $TESTDIR/tests/logs/stat.runstr
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
LIBIOR_IO=off strace -e stat -o stat_io.strace $TESTDIR/tests/bin/stat $TESTFILE
stated_file=`awk -F\" 'END{print $2}' stat_io.strace`

#test

assertEquals "Wrong stat()ed file: " "$TESTFILE" "$stated_file"

local stracestr="LIBIOR_IO=off LIBIOR_REWRITEDIR=$LIBIOR_REWRITEDIR LIBIOR_CACHEDIR=$LIBIOR_CACHEDIR LD_PRELOAD=$LD_PRELOAD $RUNSTR strace -s 256 $TESTDIR/tests/bin/stat $TESTFILE"
echo $stracestr >  $TESTDIR/tests/logs/stat.runstr
}

source "/usr/share/shunit2/shunit2"
