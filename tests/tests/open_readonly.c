#include <stdio.h>

int main(int argc,char *argv[])
{
FILE *fp;
if(argc != 2) {
	fprintf(stderr,"Usage: %s <path_to_test>\n",argv[0]);
	exit(EXIT_FAILURE);
}
fp = fopen(argv[1],"r");
fclose(fp);
exit(EXIT_SUCCESS);
}

