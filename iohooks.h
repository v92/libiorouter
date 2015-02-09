#ifndef IOHOOKS_H
#define IOHOOKS_H
int (*real_open)(const char *,int) = NULL;
int (*real_xstat)(int,const char *,struct stat *) = NULL;
int (*real_xstat64)(int,const char *,struct stat64 *) = NULL;
char *(*real_realpath)(const char *, char *) = NULL;
char *(*real_realpath_chk)(const char *, char *, size_t) = NULL;
int (*real_lxstat)(int,const char *,struct stat *) = NULL;
int (*real_lxstat64)(int,const char *,struct stat64 *) = NULL;
int (*real_unlink)(const char *path) = NULL;
/*int (*real_unlinkat)(int dirfd,const char *path,int flags) = NULL;*/
int (*real_chown)(const char *path, uid_t owner, gid_t group) = NULL;
int (*real_chmod)(const char *path,mode_t mode) = NULL;
int (*real_rmdir)(const char *path) = NULL;
int (*real_access)(const char *path, int amode) = NULL;
DIR *(*real_opendir)(const char *name) = NULL;
#endif
