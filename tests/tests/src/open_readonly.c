#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>

int main(int argc,char *argv[])
{
int fd;
if(argc != 2) {
	fprintf(stderr,"Usage: %s <path_to_test>\n",argv[0]);
	exit(EXIT_FAILURE);
}
fd = open(argv[1],O_RDONLY);
if(fd >= 0) {
	close(fd);
	exit(EXIT_SUCCESS);
}
exit(EXIT_FAILURE);
}

