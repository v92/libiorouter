#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>

int main(int argc,char *argv[])
{
struct stat sbuf;
if(argc != 2) {
	fprintf(stderr,"Usage: %s <path_to_test>\n",argv[0]);
	exit(EXIT_FAILURE);
}
exit(stat(argv[1],&sbuf));
}

