#!/bin/bash
ROOTDIR=${PWD%*/*/*}
TESTDIR=$ROOTDIR/tests
CACHEFILE=$TESTDIR/run/${TESTDIR:1}/nfsmnt/test.php
TESTFILE=$TESTDIR/nfsmnt/test.php
export LIBIOR_MAXFILESIZE=10485760
export LIBIOR_REWRITEDIR=$TESTDIR/nfsmnt
export LIBIOR_CACHEDIR=$TESTDIR/run
export LD_PRELOAD=$ROOTDIR/libiorouter.so
export testfile_size=`stat -c %s $TESTFILE`
