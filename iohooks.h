#ifndef IOHOOKS_H
#define IOHOOKS_H
int (*real_open)(const char *,int) = NULL;
void *(*real_bfd_openw)(const char *, const char *) = NULL;
FILE *(*real_fopen)(const char *,const char *) = NULL;
int (*real_creat)(const char *,int) = NULL;
int (*real_xstat)(int,const char *,struct stat *) = NULL;
int (*real_xstat64)(int,const char *,struct stat64 *) = NULL;
int (*real_fxstatat)(int,int,const char *,struct stat *,int) = NULL;
char *(*real_realpath)(const char *, char *) = NULL;
char *(*real_realpath_chk)(const char *, char *, size_t) = NULL;
int (*real_lxstat)(int,const char *,struct stat *) = NULL;
int (*real_lxstat64)(int,const char *,struct stat64 *) = NULL;
int (*real_link)(const char *,const char *) = NULL;
int (*real_symlink)(const char *,const char *) = NULL;
int (*real_rename)(const char *,const char *) = NULL;
int (*real_renameat)(int,const char *,int,const char *) = NULL;
int (*real_unlink)(const char *) = NULL;
int (*real_unlinkat)(int,const char *,int) = NULL;
int (*real_chown)(const char *, uid_t, gid_t) = NULL;
int (*real_fchownat)(int,const char *, uid_t, gid_t,int) = NULL;
int (*real_chmod)(const char *,mode_t) = NULL;
int (*real_fchmodat)(int,const char *,mode_t,int) = NULL;
int (*real_rmdir)(const char *) = NULL;
int (*real_mkdir)(const char *,mode_t) = NULL;
int (*real_access)(const char *, int) = NULL;
int (*real_faccessat)(int,const char *, int,int) = NULL;
DIR *(*real_opendir)(const char *) = NULL;
#endif
