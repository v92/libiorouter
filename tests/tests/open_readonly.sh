#!/bin/bash
tempc=`mktemp`

setUp() {
if [ ! -f ../nfsmnt/test.php ]; then
	dd if=/dev/urandom of=../nfsmnt/test.php count=10
fi
echo	"
#include <stdio.h>

int main(void)
{
FILE *fp;
fp = fopen(\"${PWD%*/*}/nfsmnt/test.php\",\"r\");
fclose(fp);
}
" > ${tempc}.c
cc -o ${tempc} ${tempc}.c

if [ ! -f ../../libiorouter.so ]; then 
	( cd ../.. && make)
fi
}

tearDown() {
	rm -f ${tempc} ${tempc}.c 
}


test_open_readonly() {
LIBIOR_IO=on LIBIOR_REWRITEDIR=${PWD%*/*}/nfsmnt LIBIOR_CACHEDIR=${PWD%*/*}/run LD_PRELOAD=${PWD%*/*/*}/libiorouter.so ${tempc}
test -f ../run/test.php
assertEquals "$?" "0"
}

source "/usr/share/shunit2/shunit2"
