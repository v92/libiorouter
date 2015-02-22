#define _GNU_SOURCE
#define __USE_BSD
#define __USE_XOPEN_EXTENDED
#include <dirent.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <errno.h>
#include <utime.h>
#include <unistd.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <syslog.h>

#include "libiorouter.h"

extern int debug_on_off;	/* 0 - debug off, 1 - debug on */
extern int trace_on_off;	/* 0 - trace off, 1 - trace on */
extern int io_on_off;	/* 0 - io routing off, 1 - io routing on */

extern int logfile_fd;
extern int stats_socket_fd;
extern struct sockaddr_in udps;

extern char *g_socket_path;
extern char *g_cache_dir;
extern size_t g_maxfilesize;
extern char *g_rewrite_dir;

extern int (*real_open)(const char *,int);
extern int (*real_creat)(const char *,int);
extern int (*real_xstat)(int,const char *,struct stat *);
extern int (*real_xstat64)(int,const char *,struct stat64 *);
extern int (*real_fxstatat)(int,int,const char *,struct stat *,int);
extern char *(*real_realpath)(const char *, char *);
extern char *(*real_realpath_chk)(const char *, char *, size_t);
extern int (*real_lxstat)(int,const char *,struct stat *);
extern int (*real_lxstat64)(int,const char *,struct stat64 *);
extern int (*real_rename)(const char *,const char *);
extern int (*real_renameat)(int,const char *,int,const char *);
extern int (*real_unlink)(const char *);
extern int (*real_unlinkat)(int,const char *,int);
extern int (*real_chown)(const char *path, uid_t , gid_t );
extern int (*real_fchownat)(int,const char *path, uid_t , gid_t,int);
extern int (*real_chmod)(const char *path, mode_t);
extern int (*real_fchmodat)(int,const char *, mode_t,int);
extern int (*real_rmdir)(const char *path);
extern int (*real_access)(const char *, int);
extern int (*real_faccessat)(int,const char *, int,int);
extern int (*real_mkdir)(const char *,mode_t);
extern DIR *(*real_opendir)(const char *);


int __lxstat64(int ver,const char *argpath,struct stat64 *buf)
{
	return __xstat(ver,argpath,(struct stat *) buf);
}

int __lxstat(int ver,const char *argpath,struct stat *buf)
{
	return __xstat(ver,argpath,(struct stat *) buf);
}

int __xstat64(int ver,const char *argpath,struct stat64 *buf)
{
	return __xstat(ver,argpath,(struct stat *) buf);
}

int __fxstatat(int ver,int dirfd,const char *argpath,struct stat *buf,int flags)
{
	char *path = NULL;
	int ret;
	path = normalize_pathat(dirfd,argpath);
	ret = __xstat(ver,path,buf);
	free(path);
	return ret;
}

int __xstat(int ver,const char *argpath,struct stat *buf)
{
	int ret = 0,ret2 = -1;
	char cachepath[PATH_MAX];
	char *path = NULL;
	char *whiteout; 
	int n; 
	struct stat cachestat;

	path = normalize_path(argpath);

	REDIRCHECK("__xstat",real_xstat,ver,path,buf);

	n = snprintf((char *) &cachepath,sizeof(cachepath),"%s%s.whiteout",g_cache_dir,path); 
	whiteout = cachepath + n - strlen(".whiteout"); 
	ret = real_access(cachepath,F_OK); 
	if(ret == 0 || (ret == -1 && errno == ENOTDIR))
			goto cleanup;
	*whiteout = '\0'; 
	if(io_on_off) {
		ret = real_xstat(ver,cachepath,&cachestat);
		if (ret == 0 || (ret == -1 && errno == ENOTDIR)) { 
			if(S_ISREG(cachestat.st_mode) && cachestat.st_size == 0) { 
				ret2 = real_xstat(ver,path,buf);
				if(!ret2) {
					if(S_ISREG(buf->st_mode))
						copy_entry(path,-1,buf,cachepath); 
				
					if(S_ISDIR(buf->st_mode)) { 
						unlink(cachepath);
						copy_entry(path,-1,buf,cachepath); 
					}
					LOGSEND(L_STATS, "HIT %s %s","__xstat",path); 
				} 
				if(ret2 == -1) {
					LOGSEND(L_STATS, "WHITEOUT %s %s","__xstat",path); 
					create_whiteout(cachepath);
				} 
				goto cleanup;
			}
			memcpy((void *) buf,(void *) &cachestat,sizeof(cachestat));
			ret2 = ret;
			goto cleanup;
		} else
			LOGSEND(L_STATS, "MISS %s %s","__xstat",path); 
	}
	ret2 = real_xstat(ver,path,buf);
	if(io_on_off && ret == -1) {
		copy_entry(path,-1,buf,cachepath);
		if(ret2 == -1) {
			create_whiteout(cachepath);
		}
	}
cleanup:
	free(path);
	return ret2;
}

int creat(const char *argpath, mode_t mode)
{
	return open(argpath,O_CREAT|O_WRONLY|O_TRUNC);
}

int open(const char *argpath,int flags,...)
{
	int ret = 0,ret2 = 0;
	char cachepath[PATH_MAX];
	char *path = NULL;

	strncpy(cachepath,g_cache_dir,PATH_MAX-1);

	path = normalize_path(argpath);

	REDIRCHECK("open",real_open,path,flags);

	/* special handling for session files */
	if(strstr(path,"sess_") && flags & O_CREAT) {
		ret2 = real_creat(path,S_IRUSR|S_IWUSR);
		goto cleanup;
	}	
	if(flags & (O_WRONLY | O_CREAT | O_TRUNC | O_DIRECTORY)) {
		snprintf(cachepath,sizeof(cachepath),"/run/%s",path);
		if(!real_access(cachepath,F_OK)) {
			real_unlink(cachepath);
		}
		snprintf(cachepath,sizeof(cachepath),"/run/%s.whiteout",path);
		if(!real_access(cachepath,F_OK)) {
			real_unlink(cachepath);
		}
		LOGSEND(L_STATS|L_JOURNAL, "CALL %s %s","openwr",path); 
		goto miss;
	}
	if(!whiteout_check(path)) {
		ret2 = -1;
		goto cleanup;
	}

	strncat(cachepath,path,sizeof(cachepath)-1);
	if(io_on_off) {
		ret = real_open(cachepath,flags);
		if (ret >= 0) { 
			struct stat cachestat;
			(void) fstat(ret,&cachestat);
			if(S_ISREG(cachestat.st_mode) && cachestat.st_size == 0) { 
				struct stat buf;
				if(!real_xstat(1,path,&buf))
					copy_entry(path,-1,&buf,cachepath); 
				goto miss;
			}
			LOGSEND(L_STATS, "HIT %s %s","open",cachepath); 
			ret2 = ret;
			goto cleanup;
		} else 
			LOGSEND(L_STATS, "MISS %s %s","open",cachepath); 
	}	
miss:
	ret2 = real_open(path,flags);
	if(io_on_off && ret == -1) {
		struct stat oldstat;
		if(ret2 > 0 && !fstat(ret2,&oldstat)) 
			copy_entry(path,ret2,&oldstat,cachepath);
		if(ret2 == -1 && errno != ENOTDIR) {
			create_whiteout(cachepath);
		}
	}
cleanup:
	free(path);
	return ret2;
}

DIR *opendir(const char *argpath)
{
	DIR *ret = NULL,*ret2 = NULL;
	char cachepath[PATH_MAX];
	char *path;
	struct dirent *dirp = NULL;
	struct dirent *prev_dirp = NULL;

	strncpy(cachepath,g_cache_dir,PATH_MAX-1);

	path = normalize_path(argpath);

	REDIRCHECK("opendir",real_opendir,path);

	if(!whiteout_check(path)) 
		goto cleanup;

	strncat(cachepath,path,sizeof(cachepath)-1);
	if(io_on_off) {
		ret = real_opendir(cachepath);
		if (ret) { 
			prev_dirp =  (struct dirent *) malloc(offsetof(struct dirent, d_name) + fpathconf(dirfd(ret),_PC_NAME_MAX) + 1);
			if(!prev_dirp) {
				ret2 = ret;
				goto cleanup;
			}
			(void) readdir_r(ret,prev_dirp,&dirp); 	 /* . */
			(void) readdir_r(ret,prev_dirp,&dirp);	 /* .. */
			(void) readdir_r(ret,prev_dirp,&dirp);   /* 1st dir entry  */

			if(dirp) {
				seekdir(ret,0);
				LOGSEND(L_STATS, "HIT %s %s","opendir",path); 
				ret2 = ret; 
				goto cleanup;
			}
		} else
			LOGSEND(L_STATS, "MISS %s %s","opendir",path); 
	}
	ret2 = real_opendir(path);
	if(io_on_off && ret) {
		struct stat oldstat;
		if(!ret2) 
			create_whiteout(cachepath);
		else if(!fstat(dirfd(ret2),&oldstat))
			copy_entry(path,-1,&oldstat,cachepath);
	}
cleanup:
	free(prev_dirp);
	free(path);
	return ret2;
}

/*
int openat(int dirfd,const char *argpath,int flags,...)
{
	char *path = NULL;
	char cachepath[PATH_MAX];
	struct stat cachestat;
	int ret = 0,ret2 = 0;

	strncpy(cachepath,g_cache_dir,PATH_MAX-1);

	if(io_on_off) {
		path = normalize_pathat(dirfd,argpath);

		REDIRCHECK("openat",real_openat,dirfd,path,flags);

		if(!whiteout_check(path)) {
			ret2 = -1;
			goto cleanup;
		}

		strncat(cachepath,path,sizeof(cachepath)-1);
		ret = real_openat(dirfd,cachepath,flags);
		if (ret >= 0) { 
			struct stat cachestat;
			(void) fstat(ret,&cachestat);
			if(S_ISREG(cachestat.st_mode) && cachestat.st_size == 0) { 
				struct stat buf;
				if(!real_xstat(1,path,&buf))
					copy_entry(path,-1,&buf,cachepath); 
				goto miss;
			}
			LOGSEND(L_STATS, "HIT %s %s","open",cachepath); 
			ret2 = ret;
			goto cleanup;
		} else 
			LOGSEND(L_STATS, "MISS %s %s","open",cachepath); 
	}

	ret2 = real_openat(dirfd,argpath,mode,flags);
	if(io_on_off && ret == -1) {
		struct stat buf;
		real_xstat(1,argpath,&buf);
		copy_entry(argpath,-1,(struct stat *) &buf,cachepath);
		if(ret2 == -1) {
			create_whiteout(cachepath);
		}
	}
cleanup:
	free(path);
	return ret2;
}
*/

int faccessat(int dirfd,const char *argpath,int mode,int flags)
{
	char *path = NULL;
	char cachepath[PATH_MAX];
	int ret = 0,ret2 = 0;

	strncpy(cachepath,g_cache_dir,PATH_MAX-1);

	if(io_on_off) {
		path = normalize_pathat(dirfd,argpath);

		REDIRCHECK("faccessat",real_faccessat,dirfd,path,mode,flags);

		if(!whiteout_check(path)) {
			ret2 = -1;
			goto cleanup;
		}

		strncat(cachepath,path,sizeof(cachepath)-1);
		ret = real_faccessat(dirfd,cachepath,mode,flags);
		if (ret >= 0) { 
			LOGSEND(L_STATS, "HIT %s %s","faccessat",cachepath); 
			ret2 = ret;
			goto cleanup;
		} else 
			LOGSEND(L_STATS, "MISS %s %s","faccessat",cachepath); 
	}

	ret2 = real_faccessat(dirfd,path,mode,flags);
	if(io_on_off && ret == -1) {
		struct stat buf;
		real_xstat(1,path,&buf);
		copy_entry(path,-1,(struct stat *) &buf,cachepath);
		if(ret2 == -1) {
			create_whiteout(cachepath);
		}
	}
cleanup:
	free(path);
	return ret2;
}

int access(const char *argpath,int mode)
{
	char *path = NULL;
	char cachepath[PATH_MAX];
	int ret = 0,ret2 = 0;

	strncpy(cachepath,g_cache_dir,PATH_MAX-1);

	path = normalize_path(argpath);

	REDIRCHECK("access",real_access,path,mode);

	if(!whiteout_check(path)) {
		ret2 = -1;
		goto cleanup;
	}

	strncat(cachepath,path,sizeof(cachepath)-1);
	if(io_on_off) {
		ret = real_access(cachepath,mode);
		if (ret >= 0) { 
			LOGSEND(L_STATS, "HIT %s %s","access",cachepath); 
			ret2 = ret;
			goto cleanup;
		} else 
			LOGSEND(L_STATS, "MISS %s %s","access",cachepath); 
	}

	ret2 = real_access(path,mode);
	if(io_on_off && ret == -1) {
		struct stat buf;
		real_xstat(1,path,&buf);
		copy_entry(path,-1,(struct stat *) &buf,cachepath);
		if(ret2 == -1) {
			create_whiteout(cachepath);
		}
	}
cleanup:
	free(path);
	return ret2;
}

int unlink(const char *argpath)
{
int ret;
char *path = NULL;
char cachepath[PATH_MAX];

strncpy(cachepath,g_cache_dir,PATH_MAX-1);

if(!argpath)
	return -1;

if(io_on_off) {
	path = normalize_path(argpath);
	REDIRCHECK("unlink",real_unlink,path);

	strncat(cachepath,path,sizeof(cachepath)-1);

	ret = real_unlink(cachepath);
	if(ret == -1)
		LOGSEND(L_STATS, "FAIL unlink %s",cachepath);
	if(ret == 0)
		LOGSEND(L_JOURNAL|L_STATS, "HIT unlink %s",path);
	free(path);
}
return real_unlink(argpath);
}

int unlinkat(int dirfd,const char *argpath,int flags)
{
char *path = NULL;
char cachepath[PATH_MAX];
int ret;

strncpy(cachepath,g_cache_dir,PATH_MAX-1);

if(!argpath)
	return -1;

path = normalize_pathat(dirfd,argpath);
REDIRCHECK("unlinkat",real_unlinkat,dirfd,path,flags);
strncat(cachepath,path,sizeof(cachepath)-1);
free(path);

ret = real_unlinkat(dirfd,cachepath,flags);
if(io_on_off && ret == -1)
	LOGSEND(L_STATS, "FAIL unlinkat %s",cachepath);
if(ret == 0)
	LOGSEND(L_JOURNAL|L_STATS, "HIT unlinkat %s",cachepath);

return real_unlinkat(dirfd,argpath,flags);
}

int fchmodat(int dirfd,const char *argpath,mode_t mode,int flags)
{
char *path = NULL;
char cachepath[PATH_MAX];
int ret;

strncpy(cachepath,g_cache_dir,PATH_MAX-1);

if(!argpath)
	return -1;

path = normalize_pathat(dirfd,argpath);

REDIRCHECK("fchmodat",real_fchmodat,dirfd,path,mode,flags);

strncat(cachepath,path,sizeof(cachepath)-1);

ret = real_fchmodat(dirfd,cachepath,mode,flags);
if(ret == -1)
	LOGSEND(L_STATS, "FAIL fchmodat %s",cachepath);
if(ret == 0)
	LOGSEND(L_JOURNAL|L_STATS, "HIT fchmodat %s",cachepath);

free(path);

return real_fchmodat(dirfd,argpath,mode,flags);
}

int chmod(const char *argpath,mode_t mode)
{
int ret;
char *path = NULL;
char cachepath[PATH_MAX];

strncpy(cachepath,g_cache_dir,PATH_MAX-1);

if(!argpath)
	return -1;

path = normalize_path(argpath);

REDIRCHECK("chmod",real_chmod,path,mode);

strncat(cachepath,path,sizeof(cachepath)-1);

ret = real_chmod(cachepath,mode);
if(ret == -1)
	LOGSEND(L_STATS, "FAIL chmod %s",cachepath);
if(ret == 0)
	LOGSEND(L_JOURNAL|L_STATS, "HIT chmod %s",cachepath);

free(path);
return real_chmod(argpath,mode);
}

int chown(const char *argpath, uid_t owner, gid_t group)
{
int ret;
char *path = NULL;
char cachepath[PATH_MAX];

strncpy(cachepath,g_cache_dir,PATH_MAX-1);

if(!argpath)
	return -1;

path = normalize_path(argpath);

REDIRCHECK("chown",real_chown,path,owner,group);

strncat(cachepath,path,sizeof(cachepath)-1);

ret = real_chown(cachepath,owner,group);
if(io_on_off && ret == -1)
	LOGSEND(L_STATS, "FAIL chown %s",cachepath);
if(ret == 0)
	LOGSEND(L_JOURNAL|L_STATS, "HIT chown %s",cachepath);

free(path);
return real_chown(argpath,owner,group);
}

int fchownat(int dirfd,const char *argpath, uid_t owner, gid_t group,int flags)
{
char *path = NULL;
char cachepath[PATH_MAX];
	int ret;

strncpy(cachepath,g_cache_dir,PATH_MAX-1);

if(!argpath)
	return -1;

path = normalize_pathat(dirfd,argpath);

REDIRCHECK("fchownat",real_fchownat,dirfd,path,owner,group,flags);

strncat(cachepath,path,sizeof(cachepath)-1);

ret = real_fchownat(dirfd,cachepath,owner,group,flags);
if(io_on_off && ret == -1)
	LOGSEND(L_STATS, "FAIL fchownat %s",cachepath);
if(ret == 0)
	LOGSEND(L_JOURNAL|L_STATS, "HIT fchownat %s",cachepath);

free(path);

return real_fchownat(dirfd,argpath,owner,group,flags);
}

int rmdir(const char *argpath)
{
int ret;
char *path = NULL;
char cachepath[PATH_MAX];

strncpy(cachepath,g_cache_dir,PATH_MAX-1);

if(!argpath)
	return -1;

path = normalize_path(argpath);

REDIRCHECK("rmdir",real_rmdir,path);
strncat(cachepath,path,sizeof(cachepath)-1);
ret = real_rmdir(cachepath);
if(io_on_off && ret == -1)
	LOGSEND(L_STATS, "FAIL rmdir %s",cachepath);
if(ret == 0)
	LOGSEND(L_JOURNAL|L_STATS, "HIT rmdir %s",cachepath);

free(path);
return real_rmdir(argpath);
}

int mkdir(const char *argpath,mode_t mode)
{
int ret;
char *path = NULL;
char cachepath[PATH_MAX];


if(!argpath)
	return -1;


path = normalize_path(argpath);

snprintf((char *) &cachepath,sizeof(cachepath),"%s%s.whiteout",g_cache_dir,path);
ret = real_access(cachepath,F_OK);
if(!ret)
	(void) real_unlink(cachepath);

REDIRCHECK("mkdir",real_mkdir,argpath,mode);
strncpy(cachepath,g_cache_dir,PATH_MAX-1);
strncat(cachepath,path,sizeof(cachepath)-1);
ret = real_mkdir(cachepath,mode);
if(io_on_off && ret == -1)
	LOGSEND(L_STATS, "FAIL mkdir %s",cachepath);
if(ret == 0)
	LOGSEND(L_JOURNAL|L_STATS, "HIT mkdir %s",cachepath);

free(path);
return real_mkdir(argpath,mode);
}

int rename(const char *oldpath,const char *newpath)
{
char *old_normpath = NULL;
char *new_normpath = NULL;
char old_cachepath[PATH_MAX];
char new_cachepath[PATH_MAX];
int ret,n;

old_normpath = normalize_path(oldpath);
if(!old_normpath)
	goto cleanup;

n = snprintf((char *) &old_cachepath,sizeof(old_cachepath),"%s%s.whiteout",g_cache_dir,old_normpath);
ret = real_access(old_cachepath,F_OK);
if(!ret)
	(void) real_unlink(old_cachepath);
old_cachepath[n - sizeof(".whiteout") + 1] = '\0';

new_normpath = normalize_path(newpath);
if(!new_normpath)
	goto cleanup;

n = snprintf((char *) &new_cachepath,sizeof(new_cachepath),"%s%s.whiteout",g_cache_dir,new_normpath);
ret = real_access(new_cachepath,F_OK);
if(!ret)
	(void) real_unlink(new_cachepath);

new_cachepath[n - sizeof(".whiteout") + 1] = '\0';
ret = real_rename(old_cachepath,new_cachepath);
if(io_on_off && ret == -1)
	LOGSEND(L_STATS, "FAIL rename %s %s",old_cachepath,new_cachepath);

if(ret == 0)
	LOGSEND(L_JOURNAL|L_STATS, "HIT rename %s %s",old_cachepath,new_cachepath);

cleanup:
	free(old_normpath);
	free(new_normpath);
	return real_rename(oldpath,newpath);
}

int renameat(int olddirfd,const char *oldpath,int newdirfd,const char *newpath)
{
char *old_normpath = NULL;
char *new_normpath = NULL;
char old_cachepath[PATH_MAX];
char new_cachepath[PATH_MAX];
int ret,n;

old_normpath = normalize_pathat(olddirfd,oldpath);
if(!old_normpath)
	goto cleanup;

n = snprintf((char *) &old_cachepath,sizeof(old_cachepath),"%s%s.whiteout",g_cache_dir,old_normpath);
ret = real_access(old_cachepath,F_OK);
if(!ret)
	(void) real_unlink(old_cachepath);
old_cachepath[n - sizeof(".whiteout") + 1] = '\0';

new_normpath = normalize_pathat(newdirfd,newpath);
if(!new_normpath)
	goto cleanup;

n = snprintf((char *) &new_cachepath,sizeof(new_cachepath),"%s%s.whiteout",g_cache_dir,new_normpath);
ret = real_access(new_cachepath,F_OK);
if(!ret)
	(void) real_unlink(new_cachepath);

new_cachepath[n - sizeof(".whiteout") + 1] = '\0';
ret = real_rename(old_cachepath,new_cachepath);
if(io_on_off && ret == -1)
	LOGSEND(L_STATS, "FAIL renameat %s %s",old_cachepath,new_cachepath);

if(ret == 0)
	LOGSEND(L_JOURNAL|L_STATS, "HIT renameat %s %s",old_cachepath,new_cachepath);

cleanup:
	free(old_normpath);
	free(new_normpath);
	return real_renameat(olddirfd,oldpath,newdirfd,newpath);
}

char *realpath(const char *path, char *resolved_path)
{
if(!path || !io_on_off || strstr(path,".snapshot")) { 
	LOGSEND(L_STATS, "CALL %s %s","realpath",path); 
	return real_realpath(path,resolved_path); 
} 

if(strncmp(path,g_rewrite_dir,strlen(g_rewrite_dir))) { 
	LOGSEND(L_STATS, "CALL %s %s","realpath",path); 
	return real_realpath(path,resolved_path); 
} 
resolved_path = libio_realpath(path);
LOGSEND(L_STATS, "HIT realpath %s",path);
return resolved_path;
}

char *__realpath_chk(const char *path, char *resolved_path,size_t resolved_len)
{
if(!path || !io_on_off || strstr(path,".snapshot")) { 
	LOGSEND(L_STATS, "CALL %s %s","__realpath_chk",path); 
	return real_realpath_chk(path,resolved_path,resolved_len); 
} 

if(strncmp(path,g_rewrite_dir,strlen(g_rewrite_dir))) { 
	LOGSEND(L_STATS, "CALL %s %s","__realpath_chk",path); 
	return real_realpath_chk(path,resolved_path,resolved_len); 
} 
resolved_path = libio_realpath_chk(path,resolved_path,resolved_len);
LOGSEND(L_STATS, "HIT realpath %s",path);
return resolved_path;
}
