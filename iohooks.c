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
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/stat.h>
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

extern int logstats_fd;
extern int logjournal_fd;
extern int stats_socket_fd;
extern struct sockaddr_in udps;

extern char *g_socket_path;
extern char *g_cache_dir;
extern size_t g_maxfilesize;
extern char *g_rewrite_dir;
extern char chroot_path[PATH_MAX];

extern int (*real_open)(const char *,int,...);
extern void *(*real_bfd_openw)(const char *,const char *);
/*extern FILE *(*real_fopen)(const char *,const char *);*/
extern int (*real_creat)(const char *,int);
extern int (*real_xstat)(int,const char *,struct stat *);
extern int (*real_xstat64)(int,const char *,struct stat64 *);
extern int (*real_fxstatat)(int,int,const char *,struct stat *,int);
extern char *(*real_realpath)(const char *, char *);
extern char *(*real_realpath_chk)(const char *, char *, size_t);
extern int (*real_lxstat)(int,const char *,struct stat *);
extern int (*real_lxstat64)(int,const char *,struct stat64 *);
extern int (*real_symlink)(const char *,const char *);
extern int (*real_link)(const char *,const char *);
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
extern int (*real_chroot)(const char *);


int chroot(const char *path)
{
if(strcmp(path,"/"))
	strncpy(chroot_path,path,PATH_MAX - 1);
else
	memset(chroot_path,'\0',PATH_MAX);	
LOGSEND(L_STATS|L_JOURNAL, "CALL %s %s","chroot",path); 
return real_chroot(path);
}

int __lxstat64(int ver,const char *argpath,struct stat64 *buf)
{
	return __xstat(ver,argpath,(struct stat *) buf);
}

int __lxstat(int ver,const char *argpath,struct stat *buf)
{
	int ret = 0,ret2 = -1,n;
	char cachepath[PATH_MAX];
	char *path = NULL;
	char *whiteout; 
	char *opstate = "CALL";
	struct stat cachestat;

	path = normalize_path(argpath);

	REDIRCHECK(path);

	n = snprintf((char *) &cachepath,sizeof(cachepath),"%s%s.whiteout",g_cache_dir,path); 
	whiteout = cachepath + n - strlen(".whiteout"); 
	ret = real_access(cachepath,F_OK); 
	if(ret == 0 || (ret == -1 && errno == ENOTDIR))
			goto cleanup;
	*whiteout = '\0'; 

	if(!io_on_off)
		goto miss;

	ret = real_lxstat(ver,cachepath,&cachestat);
	if (ret == 0 || (ret == -1 && errno == ENOTDIR)) { 
		if(S_ISREG(cachestat.st_mode) && cachestat.st_size == 0) { 
			ret2 = real_lxstat(ver,path,buf);
			if(!ret2) {
				if(S_ISREG(buf->st_mode))
					copy_entry(path,-1,buf,cachepath); 
			
				if(S_ISDIR(buf->st_mode)) { 
					real_unlink(cachepath);
					copy_entry(path,-1,buf,cachepath); 
				}
				opstate = "HIT";
			} 
			if(ret2 == -1) {
				opstate = "WHITEOUT";
				create_whiteout(cachepath);
			} 
			goto cleanup;
		}
		memcpy((void *) buf,(void *) &cachestat,sizeof(cachestat));
		ret2 = ret;
		goto cleanup;
	} 
	opstate = "MISS";
miss:
	ret2 = real_lxstat(ver,argpath,buf);
	if(io_on_off && ret == -1) {
		copy_entry(argpath,-1,buf,cachepath);
		if(ret2 == -1) 
			create_whiteout(cachepath);
	}
cleanup:
	LOGSEND(L_STATS, "%s %s %s%s",opstate,"__lxstat",chroot_path,path); 
	free(path);
	return ret2;
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
	int ret = 0,ret2 = -1,n;
	char cachepath[PATH_MAX];
	char *path = NULL;
	char *whiteout; 
	char *opstate = "CALL";
	struct stat cachestat;

	path = normalize_path(argpath);
	REDIRCHECK(path);

	n = snprintf((char *) &cachepath,sizeof(cachepath),"%s%s.whiteout",g_cache_dir,path); 
	whiteout = cachepath + n - strlen(".whiteout"); 
	ret = real_access(cachepath,F_OK); 
	if(ret == 0 || (ret == -1 && errno == ENOTDIR))
			goto cleanup;
	*whiteout = '\0'; 

	if(!io_on_off)
		goto miss;

	ret = real_xstat(ver,cachepath,&cachestat);
	if (ret == 0 || (ret == -1 && errno == ENOTDIR)) { 
		if(S_ISREG(cachestat.st_mode) && cachestat.st_size == 0) { 
			ret2 = real_xstat(ver,path,buf);
			if(!ret2) {
				if(S_ISREG(buf->st_mode))
					copy_entry(path,-1,buf,cachepath); 
			
				if(S_ISDIR(buf->st_mode)) { 
					real_unlink(cachepath);
					copy_entry(path,-1,buf,cachepath); 
				}
				opstate = "HIT";
			} 
			if(ret2 == -1) {
				opstate = "WHITEOUT";
				create_whiteout(cachepath);
			} 
			goto cleanup;
		}
		memcpy((void *) buf,(void *) &cachestat,sizeof(cachestat));
		ret2 = ret;
		goto cleanup;
	} 

	opstate = "MISS";
miss:
	ret2 = real_xstat(ver,argpath,buf);
	if(io_on_off && ret == -1) {
		copy_entry(argpath,-1,buf,cachepath);
		if(ret2 == -1) {
			create_whiteout(cachepath);
		}
	}
cleanup:
	LOGSEND(L_STATS, "%s %s %s%s",opstate,"__xstat",chroot_path,path); 
	free(path);
	return ret2;
}

/*
FILE *fopen(const char *argpath, const char *mode)
{
	int flags = O_RDONLY;
	int pathfd;
	FILE *ret;
	char *path = NULL;
	return real_fopen(argpath,mode);
	path = normalize_path(argpath);
	
	REDIRCHECK(path);

	LOGSEND(L_STATS|L_JOURNAL, "CALL %s %s","fopen",path); 
	if(strstr(mode,"w+") || strstr(mode,"r+") || strstr(mode,"a+")) {
		flags |= O_RDWR;
		if(strstr(mode,"w+"))
			flags |= O_CREAT | O_TRUNC;
		if(strstr(mode,"a+"))
			flags |= O_CREAT | O_APPEND;
	} else {
		if(index(mode,'w'))
			flags |= O_WRONLY | O_CREAT | O_TRUNC;
		if(index(mode,'a'))
			flags |= O_WRONLY | O_APPEND;
	}
	pathfd = open(path,flags);
	free(path);
	return fdopen(pathfd,mode);
	
}
*/

int creat(const char *argpath, mode_t mode)
{
	return open(argpath,O_CREAT|O_WRONLY|O_TRUNC,S_IRWXU);
}

int open(const char *argpath,int flags,...)
{
	int ret = -1,ret2 = -1,n;
	char cachepath[PATH_MAX];
	char *path = NULL;
	char *opstate = "CALL";
	int l_attr = L_STATS;

	strncpy(cachepath,g_cache_dir,PATH_MAX-1);
	path = normalize_path(argpath);

	REDIRCHECK(path);

	/* special handling for session files */
	if(strstr(path,"sess_") && flags & O_CREAT) {
		ret2 = real_creat(path,S_IRUSR|S_IWUSR);
		goto cleanup;
	}	
	if(flags & (O_WRONLY | O_RDWR | O_APPEND | O_CREAT | O_TRUNC | O_DIRECTORY)) {
		snprintf(cachepath,sizeof(cachepath),"%s%s",g_cache_dir,path);
		if(!real_access(cachepath,F_OK)) {
			real_unlink(cachepath);
		}
		n = snprintf(cachepath,sizeof(cachepath),"%s%s.whiteout",g_cache_dir,path);
		if(!real_access(cachepath,F_OK)) {
			real_unlink(cachepath);
		}
		cachepath[n - sizeof(".whiteout") + 1] = '\0';
		l_attr = L_STATS | L_JOURNAL;
		opstate = "CALL";
		goto miss;
	} 

	if(!whiteout_check(path)) 
		goto cleanup;

	strncat(cachepath,path,sizeof(cachepath)-1);
	if(!io_on_off) 
		goto miss;

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
		opstate = "HIT";
		ret2 = ret;
		goto cleanup;
	}

	opstate = "MISS";
miss:
	ret2 = real_open(argpath,flags,S_IRWXU|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH);
	if(io_on_off && ret == -1) {
		struct stat oldstat;
		if(ret2 >= 0 && !fstat(ret2,&oldstat))  {
			if(flags == O_RDONLY)
				copy_entry(argpath,ret2,&oldstat,cachepath);
		}
		if(ret2 == -1 && errno != ENOTDIR) {
			create_whiteout(cachepath);
		}
	}
cleanup:
	LOGSEND(l_attr, "%s %s %s%s",opstate,"open",chroot_path,path); 
	free(path);
	return ret2;
}

/*
DIR *opendir(const char *argpath)
{
	DIR *ret = NULL,*ret2 = NULL;
	char cachepath[PATH_MAX];
	char *path;
	char *opstate = "CALL";
	struct dirent *dirp = NULL;
	struct dirent *prev_dirp = NULL;

	strncpy(cachepath,g_cache_dir,PATH_MAX-1);

	path = normalize_path(argpath);

        if(!path) {
                errno = EFAULT; 
                return NULL; 
        } 

	strncat(cachepath,path,sizeof(cachepath)-1);

        if(     strstr(path,".snapshot") ||
                strncmp(path,g_rewrite_dir,strlen(g_rewrite_dir)) || 
                strncmp(chroot_path,g_rewrite_dir,strlen(g_rewrite_dir))
        ) { 
                goto miss;
        }



	if(!whiteout_check(path)) 
		goto cleanup;


	if(!io_on_off)
		goto miss;
	
	ret = real_opendir(cachepath);
	if (ret) { 
		prev_dirp =  (struct dirent *) malloc(offsetof(struct dirent, d_name) + fpathconf(dirfd(ret),_PC_NAME_MAX) + 1);
		if(!prev_dirp) 
			goto miss;

		(void) readdir_r(ret,prev_dirp,&dirp); 	 
		(void) readdir_r(ret,prev_dirp,&dirp);
		(void) readdir_r(ret,prev_dirp,&dirp);

		if(dirp) {
			seekdir(ret,0);
			opstate = "HIT";
			ret2 = ret; 
			goto cleanup;
		}
	} 
	opstate = "MISS";
miss:
	ret2 = real_opendir(argpath);
	if(io_on_off && !ret) {
		struct stat oldstat;
		if(!ret2) 
			create_whiteout(cachepath);
		else if(!fstat(dirfd(ret2),&oldstat))
			copy_entry(path,-1,&oldstat,cachepath);
	}
cleanup:
	LOGSEND(L_STATS, "%s %s %s%s",opstate,"opendir",chroot_path,path); 
	free(prev_dirp);
	free(path);
	return ret2;
}
*/
void *bfd_openw(const char *filename,const char *target)
{
	char whiteoutpath[PATH_MAX];
	char *path = NULL;
	int n,tmp;
	void *ret;
	tmp = io_on_off;	
	io_on_off = 0;
	LOGSEND(L_STATS|L_JOURNAL, "CALL %s %s %s%s","openwr",filename,chroot_path,target); 

	n = snprintf(whiteoutpath,PATH_MAX,"%s%s.whiteout",g_cache_dir,path);
	if(!real_access(whiteoutpath,F_OK)) 
		real_unlink(whiteoutpath);
	whiteoutpath[n - sizeof(".whiteout") + 1] = '\0';
	if(!real_access(whiteoutpath,F_OK)) 
		real_unlink(whiteoutpath);
	ret = real_bfd_openw(filename,target);
	free(path);
	io_on_off = tmp;
	return ret;
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

		REDIRCHECK(path);

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
	int ret = 0,ret2 = -1;
	char *path = NULL;
	char cachepath[PATH_MAX];
	char *opstate = "CALL";

	strncpy(cachepath,g_cache_dir,PATH_MAX-1);
	path = normalize_pathat(dirfd,argpath);
	strncat(cachepath,path,sizeof(cachepath)-1);

	REDIRCHECK(path);

	if(!whiteout_check(path))
		goto cleanup;

	if(!io_on_off)
		goto miss;

	ret2 = ret = real_faccessat(dirfd,cachepath,mode,flags);
	if (ret >= 0) { 
		opstate = "HIT";
		goto cleanup;
	}
	opstate = "MISS";
miss:
	ret2 = real_faccessat(dirfd,argpath,mode,flags);
	if(io_on_off && ret == -1) {
		struct stat buf;
		real_xstat(1,argpath,&buf);
		copy_entry(argpath,-1,(struct stat *) &buf,cachepath);
		if(ret2 == -1) 
			create_whiteout(cachepath);
	}
cleanup:
	LOGSEND(L_STATS, "%s %s %s%s",opstate,"faccessat",chroot_path,cachepath); 
	free(path);
	return ret2;
}

int access(const char *argpath,int mode)
{
	int ret = 0,ret2 = -1;
	char *path = NULL;
	char *opstate = "CALL";
	char cachepath[PATH_MAX];

	strncpy(cachepath,g_cache_dir,PATH_MAX-1);
	path = normalize_path(argpath);
	strncat(cachepath,path,sizeof(cachepath)-1);

	REDIRCHECK(path);

	if(!whiteout_check(path))
		goto cleanup;

	if(!io_on_off) 
		goto miss;

	ret2 = ret = real_access(cachepath,mode);
	if (ret >= 0) { 
		opstate = "HIT";
		goto cleanup;
	}
	opstate = "MISS";
miss:
	ret2 = real_access(argpath,mode);
	if(io_on_off && ret == -1) {
		struct stat buf;
		real_xstat(1,argpath,&buf);
		copy_entry(argpath,-1,(struct stat *) &buf,cachepath);
		if(ret2 == -1)
			create_whiteout(cachepath);
	}
cleanup:
	LOGSEND(L_STATS, "%s %s %s%s",opstate,"access",chroot_path,cachepath); 
	free(path);
	return ret2;
}

int unlink(const char *argpath)
{
int ret;
int l_attr = L_STATS;
char *path = NULL;
char *opstate = "CALL";
char cachepath[PATH_MAX];

strncpy(cachepath,g_cache_dir,PATH_MAX-1);
path = normalize_path(argpath);
strncat(cachepath,path,sizeof(cachepath)-1);

REDIRCHECK(path);

ret = real_unlink(cachepath);
if(ret == -1) 
	opstate = "FAIL";
if(ret == 0) {
	opstate = "HIT";
	l_attr = L_JOURNAL|L_STATS;
}
miss:
	LOGSEND(l_attr, "%s %s %s%s",opstate,"unlink",chroot_path,argpath);
	free(path);
	return real_unlink(argpath);
}

int unlinkat(int dirfd,const char *argpath,int flags)
{
int ret;
int l_attr = L_STATS;
char *path = NULL;
char *opstate = "CALL";
char cachepath[PATH_MAX];

if(!argpath)
	return -1;

path = normalize_pathat(dirfd,argpath);

snprintf((char *) &cachepath,sizeof(cachepath),"%s%s.whiteout",g_cache_dir,path);
ret = real_access(cachepath,F_OK);
if(!ret)
	(void) real_unlink(cachepath);

strncpy(cachepath,g_cache_dir,PATH_MAX-1);

REDIRCHECK(path);
strncat(cachepath,path,sizeof(cachepath)-1);

ret = real_unlinkat(dirfd,cachepath,flags);
if(io_on_off && ret == -1)
	opstate = "FAIL";
if(ret == 0) {
	opstate = "HIT";
	l_attr = L_STATS|L_JOURNAL;
}

miss:
	LOGSEND(l_attr, "%s %s %s%s",opstate,"unlinkat",chroot_path,cachepath);
	free(path);
	return real_unlinkat(dirfd,argpath,flags);
}

int fchmodat(int dirfd,const char *argpath,mode_t mode,int flags)
{
int ret;
int l_attr = L_STATS;
char *path = NULL;
char *opstate = "CALL";
char cachepath[PATH_MAX];

strncpy(cachepath,g_cache_dir,PATH_MAX-1);

if(!argpath)
	return -1;

path = normalize_pathat(dirfd,argpath);

REDIRCHECK(path);

strncat(cachepath,path,sizeof(cachepath)-1);

ret = real_fchmodat(dirfd,cachepath,mode,flags);
if(ret == -1)
	opstate = "FAIL";
if(ret == 0) {
	opstate = "HIT";
	l_attr = L_STATS|L_JOURNAL;
}

miss:
	LOGSEND(l_attr,"%s %s %s%s",opstate,"fchmodat",chroot_path,cachepath);
	free(path);
	return real_fchmodat(dirfd,argpath,mode,flags);
}

int chmod(const char *argpath,mode_t mode)
{
int ret;
int l_attr = L_STATS;
char *path = NULL;
char *opstate = "CALL";
char cachepath[PATH_MAX];

strncpy(cachepath,g_cache_dir,PATH_MAX-1);
path = normalize_path(argpath);
strncat(cachepath,path,sizeof(cachepath)-1);

REDIRCHECK(path);

ret = real_chmod(cachepath,mode);
if(ret == -1)
	opstate = "FAIL";
if(ret == 0) {
	opstate = "HIT";
	l_attr = L_STATS|L_JOURNAL;
}
miss:
	LOGSEND(l_attr,"%s %s %s%s",opstate,"chmod",chroot_path,cachepath);
	free(path);
	return real_chmod(argpath,mode);
}

int chown(const char *argpath, uid_t owner, gid_t group)
{
int ret;
int l_attr = L_STATS;
char *path = NULL;
char *opstate = "CALL";
char cachepath[PATH_MAX];

strncpy(cachepath,g_cache_dir,PATH_MAX-1);
path = normalize_path(argpath);
strncat(cachepath,path,sizeof(cachepath)-1);

REDIRCHECK(path);

ret = real_chown(cachepath,owner,group);
if(ret == -1)
	opstate = "FAIL";
if(ret == 0) {
	opstate = "HIT";
	l_attr = L_STATS|L_JOURNAL;
}

miss:
	LOGSEND(l_attr,"%s %s %s%s",opstate,"chown",chroot_path,cachepath);
	free(path);
	return real_chown(argpath,owner,group);
}

int fchownat(int dirfd,const char *argpath, uid_t owner, gid_t group,int flags)
{
int ret;
int l_attr = L_STATS;
char *path = NULL;
char *opstate = "CALL";
char cachepath[PATH_MAX];

strncpy(cachepath,g_cache_dir,PATH_MAX-1);
path = normalize_pathat(dirfd,argpath);
strncat(cachepath,path,sizeof(cachepath)-1);

REDIRCHECK(path);

ret = real_fchownat(dirfd,cachepath,owner,group,flags);
if(ret == -1)
	opstate = "FAIL";
if(ret == 0) {
	opstate = "HIT";
        l_attr = L_STATS|L_JOURNAL;
}

miss:
	LOGSEND(l_attr,"%s %s %s%s",opstate,"fchownat",chroot_path,cachepath);
	free(path);
	return real_fchownat(dirfd,argpath,owner,group,flags);
}

int rmdir(const char *argpath)
{
int ret;
int l_attr = L_STATS;
char *path = NULL;
char *opstate = "CALL";
char cachepath[PATH_MAX];

strncpy(cachepath,g_cache_dir,PATH_MAX-1);
path = normalize_path(argpath);
strncat(cachepath,path,sizeof(cachepath)-1);

REDIRCHECK(path);

ret = real_rmdir(cachepath);
if(ret == -1)
	opstate = "FAIL";
if(ret == 0) {
	opstate = "HIT";
	l_attr = L_STATS|L_JOURNAL;
}

miss:
	LOGSEND(l_attr,"%s %s %s%s",opstate,"rmdir",chroot_path,cachepath);
	free(path);
	return real_rmdir(argpath);
}

int mkdir(const char *argpath,mode_t mode)
{
int ret;
int l_attr = L_STATS;
char *path = NULL;
char *opstate = "CALL";
char cachepath[PATH_MAX];

strncpy(cachepath,g_cache_dir,PATH_MAX-1);
path = normalize_path(argpath);
strncat(cachepath,path,sizeof(cachepath)-1);

if(!argpath)
	return -1;

snprintf((char *) &cachepath,sizeof(cachepath),"%s%s.whiteout",g_cache_dir,path);
ret = real_access(cachepath,F_OK);
if(!ret)
	(void) real_unlink(cachepath);

REDIRCHECK(path);
strncpy(cachepath,g_cache_dir,PATH_MAX-1);
strncat(cachepath,path,sizeof(cachepath)-1);
ret = real_mkdir(cachepath,mode);
if(ret == -1)
	opstate = "FAIL";
if(ret == 0) {
	opstate = "HIT";
	l_attr = L_STATS | L_JOURNAL;
}

miss:
	LOGSEND(l_attr,"%s %s %s%s",opstate,"rmdir",chroot_path,cachepath);
	free(path);
	return real_mkdir(argpath,mode);
}

int link(const char *oldpath,const char *newpath)
{
int ret,n;
int l_attr = L_STATS;
char *opstate = "CALL";
char *old_normpath = NULL;
char *new_normpath = NULL;
char old_cachepath[PATH_MAX];
char new_cachepath[PATH_MAX];

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
ret = real_link(old_cachepath,new_cachepath);
if(ret == -1)
	opstate = "FAIL";

if(ret == 0) {
	opstate = "HIT";
	l_attr = L_STATS | L_JOURNAL;
}

cleanup:
	LOGSEND(l_attr,"%s %s %s%s %s%s",opstate,"link",chroot_path,oldpath,chroot_path,newpath);
	free(old_normpath);
	free(new_normpath);
	return real_link(oldpath,newpath);
}

int symlink(const char *oldpath,const char *newpath)
{
int ret,n;
int l_attr = L_STATS;
char *opstate = "CALL";
char *old_normpath = NULL;
char *new_normpath = NULL;
char old_cachepath[PATH_MAX];
char new_cachepath[PATH_MAX];

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
ret = real_symlink(old_cachepath,new_cachepath);
if(ret == -1)
	opstate = "MISS";

if(ret == 0) {
	opstate = "HIT";
	l_attr = L_STATS|L_JOURNAL;
}

cleanup:
	LOGSEND(l_attr,"%s %s %s%s %s%s",opstate,"symlink",chroot_path,oldpath,chroot_path,newpath);
	free(old_normpath);
	free(new_normpath);
	return real_symlink(oldpath,newpath);
}

int rename(const char *oldpath,const char *newpath)
{
int ret,n;
int l_attr = L_STATS;
char *opstate = "CALL";
char *old_normpath = NULL;
char *new_normpath = NULL;
char old_cachepath[PATH_MAX];
char new_cachepath[PATH_MAX];

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
if(ret == -1)
	opstate = "MISS";

if(ret == 0) {
	opstate = "HIT";
	l_attr = L_STATS | L_JOURNAL;
}

cleanup:
	LOGSEND(l_attr,"%s %s %s%s %s%s",opstate,"rename",chroot_path,oldpath,chroot_path,newpath);
	free(old_normpath);
	free(new_normpath);
	return real_rename(oldpath,newpath);
}

int renameat(int olddirfd,const char *oldpath,int newdirfd,const char *newpath)
{
int ret,n;
int l_attr = L_STATS;
char *opstate = "CALL";
char *old_normpath = NULL;
char *new_normpath = NULL;
char old_cachepath[PATH_MAX];
char new_cachepath[PATH_MAX];

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
if(ret == -1)
	opstate = "MISS";

if(ret == 0) {
	opstate = "HIT";
	l_attr = L_STATS|L_JOURNAL;
}

cleanup:
	LOGSEND(l_attr,"%s %s %s%s %s%s",opstate,"renameat",chroot_path,oldpath,chroot_path,newpath);
	free(old_normpath);
	free(new_normpath);
	return real_renameat(olddirfd,oldpath,newdirfd,newpath);
}

char *realpath(const char *path, char *resolved_path)
{
if(!path || !io_on_off || strstr(path,".snapshot"))
	return real_realpath(path,resolved_path); 

if(strncmp(path,g_rewrite_dir,strlen(g_rewrite_dir)))
	return real_realpath(path,resolved_path); 

LOGSEND(L_STATS, "CALL %s %s","realpath",path); 
resolved_path = libio_realpath(path);
return resolved_path;
}

char *__realpath_chk(const char *path, char *resolved_path,size_t resolved_len)
{
if(!path || !io_on_off || strstr(path,".snapshot"))
	return real_realpath_chk(path,resolved_path,resolved_len); 

if(strncmp(path,g_rewrite_dir,strlen(g_rewrite_dir)))
	return real_realpath_chk(path,resolved_path,resolved_len); 

LOGSEND(L_STATS, "CALL %s %s","__realpath_chk",path); 
resolved_path = libio_realpath_chk(path,resolved_path,resolved_len);
return resolved_path;
}
