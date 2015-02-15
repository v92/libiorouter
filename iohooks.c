#define _GNU_SOURCE
#define __USE_BSD
#define __USE_XOPEN_EXTENDED
#include <dirent.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

extern int (*real_open)(const char *,int);
extern int (*real_xstat)(int,const char *,struct stat *);
extern int (*real_xstat64)(int,const char *,struct stat64 *);
extern char *(*real_realpath)(const char *, char *);
extern char *(*real_realpath_chk)(const char *, char *, size_t);
extern int (*real_lxstat)(int,const char *,struct stat *);
extern int (*real_lxstat64)(int,const char *,struct stat64 *);
extern int (*real_unlink)(const char *);
extern int (*real_unlinkat)(int,const char *,int);
extern int (*real_chown)(const char *path, uid_t , gid_t );
extern int (*real_fchownat)(int,const char *path, uid_t , gid_t,int);
extern int (*real_chmod)(const char *path, mode_t);
extern int (*real_fchmodat)(int,const char *, mode_t,int);
extern int (*real_rmdir)(const char *path);
extern int (*real_access)(const char *, int);
extern DIR *(*real_opendir)(const char *);


int __lxstat64(int ver,const char *path,struct stat64 *buf)
{
	return __xstat(ver,path,(struct stat *) buf);
}

int __lxstat(int ver,const char *argpath,struct stat *buf)
{
	return __xstat(ver,argpath,(struct stat *) buf);
}

int __xstat64(int ver,const char *argpath,struct stat64 *buf)
{
	return __xstat(ver,argpath,(struct stat *) buf);
}

int __xstat(int ver,const char *argpath,struct stat *buf)
{
	int ret = 0,ret2 = -1;
	char cachepath[PATH_MAX] = { CACHEDIR };
	char *path = NULL;
	char *whiteout; 
	int n; 
	struct stat cachestat;

	path = normalize_path(argpath,strlen(argpath));

	REDIRCHECK("__xstat",real_xstat,ver,argpath,buf);

	n = snprintf((char *) &cachepath,sizeof(cachepath),"%s%s.whiteout",CACHEDIR,path); 
	whiteout = cachepath + n - strlen(".whiteout"); 
	ret = real_access(cachepath,F_OK); 
	if(ret == 0 || (ret == -1 && errno == ENOTDIR))
			goto cleanup;
	*whiteout = '\0'; 
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
	} else { 
		LOGSEND(L_STATS, "MISS %s %s","__xstat",path); 
		ret2 = real_xstat(ver,path,buf);
#ifdef RWCACHE
		if(ret == -1) {
			copy_entry(path,-1,buf,cachepath);
			if(ret2 == -1) {
				create_whiteout(cachepath);
			}
		}
#endif
	} 
cleanup:
	free(path);
	return ret2;
}

int open(const char *argpath,int flags,...)
{
	int ret = 0,ret2 = 0;
	char cachepath[PATH_MAX] = { CACHEDIR };
	char *path = NULL;

	path = normalize_path(argpath,strlen(argpath));

	REDIRCHECK("open",real_open,path,flags);

	/* special handling for session files */
	if(strstr(path,"sess_") && flags & O_CREAT) {
		ret2 = creat(path,S_IRUSR|S_IWUSR);
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

	strncat(cachepath,path,sizeof(cachepath));
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
	}
	
miss:
	LOGSEND(L_STATS, "MISS %s %s","open",cachepath); 
	ret2 = real_open(path,flags);
#ifdef RWCACHE
	if(ret == -1) {
		struct stat oldstat;
		if(ret2 > 0 && !fstat(ret2,&oldstat)) 
			copy_entry(path,ret2,&oldstat,cachepath);
		if(ret2 == -1 && errno != ENOTDIR) {
			create_whiteout(cachepath);
		}
	}
#endif
cleanup:
	free(path);
	return ret2;
}

DIR *opendir(const char *argpath)
{
	DIR *ret = NULL,*ret2 = NULL;
	char cachepath[PATH_MAX] = { CACHEDIR };
	char *path;

	path = normalize_path(argpath,strlen(argpath));

	REDIRCHECK("opendir",real_opendir,path);

	if(!whiteout_check(path)) {
		goto cleanup;
	}

	strncat(cachepath,path,sizeof(cachepath));
	ret = real_opendir(cachepath);
	if (ret) { 
		readdir(ret);	 /* . */
		readdir(ret);	 /* .. */	
		if(readdir(ret)) {
			seekdir(ret,0);
			LOGSEND(0, "H %s %s","opendir",path); 
			ret2 = ret; 
			goto cleanup;
		}
	} 
	ret2 = real_opendir(path);
	LOGSEND(0, "M %s %s","opendir",path); 
#ifdef RWCACHE
	if(!ret) {
		struct stat oldstat;
		if(!ret2) 
			create_whiteout(cachepath);
		if(!fstat(dirfd(ret2),&oldstat))
			copy_entry(path,-1,&oldstat,cachepath);
	}
#endif
cleanup:
	free(path);
	return ret2;
}
/*
int openat(int dirfd,const char *path,int flags,...)
{
	int ret = 0,ret2 = 0;
	char cachepath[PATH_MAX];
	struct stat oldstat;

	LOGSEND(0, "CALL openat %s",path);
	REDIRCHECK("openat",real_openat,dirfd,path,flags);
	
	ret2 = real_openat(dirfd,path,flags);
#ifdef RWCACHE
	if(ret == -1) {
		if(ret2 > 0 && !fstat(ret2,&oldstat))
			copy_entry(path,ret2,&oldstat,cachepath);
		if(ret2 == -1) {
			create_whiteout(cachepath);
		}
	}
#endif
	return ret2;
}
*/

int access(const char *argpath,int amode)
{
	char *path = NULL;
	char cachepath[PATH_MAX] = { CACHEDIR };
	int ret = 0,ret2 = 0;

	path = normalize_path(argpath,strlen(argpath));

	REDIRCHECK("access",real_access,path,amode);

	if(!whiteout_check(path)) {
		ret2 = -1;
		goto cleanup;
	}

	strncat(cachepath,path,sizeof(cachepath));
	ret = real_access(cachepath,amode);
	if (ret >= 0) { 
		LOGSEND(L_STATS, "HIT %s %s","access",cachepath); 
		ret2 = ret;
		goto cleanup;
	} else 
		LOGSEND(L_STATS, "MISS %s %s","access",cachepath); 


	ret2 = real_access(path,amode);
#ifdef RWCACHE
	if(ret == -1) {
		struct stat buf;
		real_xstat(1,path,&buf);
		copy_entry(path,-1,(struct stat *) &buf,cachepath);
		if(ret2 == -1) {
			create_whiteout(cachepath);
		}
	}
#endif
cleanup:
	free(path);
	return ret2;
}

int unlink(const char *argpath)
{
int ret;
char *path = NULL;
char cachepath[PATH_MAX] = { CACHEDIR };

if(!argpath)
	return -1;

path = normalize_path(argpath,strlen(argpath));

REDIRCHECK("unlink",real_unlink,path);

strncat(cachepath,path,PATH_MAX);

ret = real_unlink(cachepath);
if(ret == -1)
	LOGSEND(L_STATS, "FAIL unlink %s",cachepath);
if(ret == 0)
	LOGSEND(L_JOURNAL|L_STATS, "HIT unlink %s",path);

ret = real_unlink(path);
free(path);
return ret;
}

int unlinkat(int dirfd,const char *argpath,int flags)
{
int ret;
char *path = NULL;
char cachepath[PATH_MAX] = { CACHEDIR };
if(!argpath)
	return -1;
if(*argpath == '/' && dirfd == AT_FDCWD) {
	path = normalize_path(argpath,strlen(argpath));

	REDIRCHECK("unlinkat",real_unlinkat,dirfd,path,flags);

	strncat(cachepath,path,PATH_MAX);

	ret = real_unlinkat(dirfd,cachepath,flags);
	if(ret == -1)
		LOGSEND(L_STATS, "FAIL unlinkat %s",cachepath);
	if(ret == 0)
		LOGSEND(L_JOURNAL|L_STATS, "HIT unlinkat %s",path);

	ret = real_unlinkat(dirfd,path,flags);
	free(path);
	return ret;
} 
ret = real_unlinkat(dirfd,argpath,flags);
return ret;
}

int fchmodat(int dirfd,const char *argpath,mode_t mode,int flags)
{
int ret;
char *path = NULL;
char cachepath[PATH_MAX] = { CACHEDIR };

if(!argpath)
	return -1;
if(*argpath == '/' && dirfd == AT_FDCWD) {
	path = normalize_path(argpath,strlen(argpath));

	REDIRCHECK("fchmodat",real_fchmodat,dirfd,path,mode,flags);

	strncat(cachepath,path,PATH_MAX);

	ret = real_fchmodat(dirfd,cachepath,mode,flags);
	if(ret == -1)
		LOGSEND(L_STATS, "FAIL fchmodat %s",cachepath);
	if(ret == 0)
		LOGSEND(L_JOURNAL|L_STATS, "HIT fchmodat %s",cachepath);

	ret = real_fchmodat(dirfd,path,mode,flags);
	free(path);
	return ret;
} 
ret = real_fchmodat(dirfd,argpath,mode,flags);
return ret;
}

int chmod(const char *argpath,mode_t mode)
{
int ret;
char *path = NULL;
char cachepath[PATH_MAX] = { CACHEDIR };

if(!argpath)
	return -1;

path = normalize_path(argpath,strlen(argpath));

REDIRCHECK("chmod",real_chmod,path,mode);

strncat(cachepath,path,PATH_MAX);

ret = real_chmod(cachepath,mode);
if(ret == -1)
	LOGSEND(L_STATS, "FAIL chmod %s",cachepath);
if(ret == 0)
	LOGSEND(L_JOURNAL|L_STATS, "HIT chmod %s",cachepath);

ret = real_chmod(path,mode);
free(path);
return ret;
}

int chown(const char *argpath, uid_t owner, gid_t group)
{
int ret;
char *path = NULL;
char cachepath[PATH_MAX] = { CACHEDIR };

if(!argpath)
	return -1;

path = normalize_path(argpath,strlen(argpath));

REDIRCHECK("chown",real_chown,path,owner,group);

strncat(cachepath,path,PATH_MAX);

ret = real_chown(cachepath,owner,group);
if(ret == -1)
	LOGSEND(L_STATS, "FAIL chown %s",cachepath);
if(ret == 0)
	LOGSEND(L_JOURNAL|L_STATS, "HIT chown %s",cachepath);

ret = real_chown(path,owner,group);
free(path);
return ret;
}

int fchownat(int dirfd,const char *argpath, uid_t owner, gid_t group,int flags)
{
int ret;
char *path = NULL;
char cachepath[PATH_MAX] = { CACHEDIR };

if(!argpath)
	return -1;
if(*argpath == '/' && dirfd == AT_FDCWD) {
	path = normalize_path(argpath,strlen(argpath));

	REDIRCHECK("fchownat",real_fchownat,dirfd,path,owner,group,flags);

	strncat(cachepath,path,PATH_MAX);

	ret = real_fchownat(dirfd,cachepath,owner,group,flags);
	if(ret == -1)
		LOGSEND(L_STATS, "FAIL fchownat %s",cachepath);
	if(ret == 0)
		LOGSEND(L_JOURNAL|L_STATS, "HIT fchownat %s",cachepath);

	ret = real_fchownat(dirfd,cachepath,owner,group,flags);
	free(path);
	return ret;
}
ret = real_fchownat(dirfd,path,owner,group,flags);
return ret;
}

int rmdir(const char *argpath)
{
int ret;
char *path = NULL;
char cachepath[PATH_MAX] = { CACHEDIR };

if(!argpath)
	return -1;

path = normalize_path(argpath,strlen(argpath));

REDIRCHECK("rmdir",real_rmdir,path);

strncat(cachepath,path,PATH_MAX);

ret = real_rmdir(cachepath);
if(ret == -1)
	LOGSEND(L_STATS, "FAIL rmdir %s",cachepath);
if(ret == 0)
	LOGSEND(L_JOURNAL|L_STATS, "HIT rmdir %s",cachepath);

ret = real_rmdir(path);
free(path);
return ret;
}

char *realpath(const char *path, char *resolved_path)
{
REDIRCHECK("realpath %s",real_realpath,path,resolved_path);

if(!strncmp(path,REWRITEDIR,strlen(REWRITEDIR))) { 
	resolved_path = libio_realpath(path);
	LOGSEND(L_STATS, "HIT realpath %s",path);
	return resolved_path;
} 
	LOGSEND(L_STATS, "MISS realpath %s",path);
return real_realpath(path,resolved_path);
}

char *__realpath_chk(const char *path, char *resolved_path,size_t resolved_len)
{
REDIRCHECK("__realpath_chk %s",real_realpath_chk,path,resolved_path,resolved_len);
if(!strncmp(path,REWRITEDIR,strlen(REWRITEDIR))) { 
	LOGSEND(L_STATS, "HIT __realpath_chk %s",path);
	return libio_realpath_chk(path,resolved_path,resolved_len);
} 
	LOGSEND(L_STATS, "MISS __realpath_chk %s",path);
return real_realpath_chk(path,resolved_path,resolved_len);
}
