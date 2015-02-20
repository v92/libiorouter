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
#include "iohooks.h"

#define HOOK(iotohook,ioptr) \
	if(!ioptr) { ioptr = dlsym(RTLD_NEXT,iotohook); }



int debug_on_off = 1;	/* 0 - debug off, 1 - debug on */
int trace_on_off = 1;	/* 0 - trace off, 1 - trace on */
int io_on_off = 1;	/* 0 - io routing off, 1 - io routing on */

int logfile_fd = -1;
int stats_socket_fd = -1;
struct sockaddr_in udps;

char *g_socket_path = NULL;
char *g_cache_dir = NULL;
size_t g_maxfilesize = 0;
char *g_rewrite_dir = NULL;

void ioonoff(int signum)
{
if(io_on_off == 1)
	io_on_off = 0;

if(io_on_off == 0)
	io_on_off = 1;
}

void traceonoff(int signum)
{
if(trace_on_off == 1)
	trace_on_off = 0;

if(trace_on_off == 0)
	trace_on_off = 1;
}

void debugonoff(int signum)
{
if(debug_on_off == 1)
	debug_on_off = 0;

if(debug_on_off == 0)
	debug_on_off = 1;
}

void init_global_vars(void)
{
char *tmp;

if((tmp = getenv("LIBIOR_SOCK_PATH")) != NULL)
	g_socket_path = strdup(tmp);
else
	g_socket_path = DEFAULT_SOCK_PATH;

if((tmp = getenv("LIBIOR_CACHEDIR")) != NULL)
	g_cache_dir = strdup(tmp);
else
	g_cache_dir = DEFAULT_CACHEDIR;

if((tmp = getenv("LIBIOR_REWRITEDIR")) != NULL)
	g_rewrite_dir = strdup(tmp);
else
	g_rewrite_dir = DEFAULT_REWRITEDIR;

if((tmp = getenv("LIBIOR_MAXFILESIZE")) != NULL)
	g_maxfilesize = atoi(tmp);
else
	g_maxfilesize = DEFAULT_MAXFILESIZE;

if((tmp = getenv("LIBIOR_IO_OFF")) != NULL)
	io_on_off = atoi(tmp) == 1 ? 0 : 1;

syslog(3, "libiorouter: stats socket set to '%s'", g_socket_path);
syslog(3, "libiorouter: cache dir set to '%s'", g_cache_dir);
syslog(3, "libiorouter: rewrite dir set to '%s'", g_rewrite_dir);
syslog(3, "libiorouter: max file size set to '%d'", (int) g_maxfilesize);
syslog(3, "libiorouter: IO routing is %s", io_on_off == 1 ? "on": "off");
return;
}

static void libiorouter_init(void) __attribute__ ((constructor));

static void libiorouter_init(void) 
{
HOOK("open",real_open);
HOOK("opendir",real_opendir);
HOOK("chmod",real_chmod);
HOOK("fchmodat",real_fchmodat);
HOOK("chown",real_chown);
HOOK("fchownat",real_fchownat);
HOOK("realpath",real_realpath);
HOOK("unlink",real_unlink);
HOOK("unlinkat",real_unlinkat);
HOOK("access",real_access);
HOOK("faccess",real_faccessat);
HOOK("rmdir",real_rmdir);
	
HOOK("__xstat",real_xstat);
HOOK("__xstat64",real_xstat64);
HOOK("fxstatat",real_fxstatat);
HOOK("__realpath_chk",real_realpath_chk);
HOOK("__lxstat",real_lxstat);
HOOK("__lxstat64",real_lxstat64);

init_global_vars();
reinit_log_file(SIGPROF);
signal(SIGTTIN,traceonoff);	
signal(SIGTTOU,debugonoff);	
signal(SIGURG,ioonoff);	
signal(SIGPROF,reinit_log_file);	
socket_init();
return;
}
#define UNIX_PATH_MAX 108

void socket_init(void)
{
if ((stats_socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) 
	syslog(3, "stats socket '%s' initialization failed: %s", g_socket_path, strerror(errno));

memset((char *) &udps, 0, sizeof(udps));
udps.sin_family = AF_INET;
udps.sin_port = htons(12345);
inet_aton("127.0.0.1", &udps.sin_addr);
     
/*
struct sockaddr_un remote;

if ((stats_socket_fd = socket(AF_UNIX, SOCK_DGRAM, 0)) == -1) 
	syslog(3, "stats socket '%s' initialization failed: %s", g_socket_path, strerror(errno));

remote.sun_family = AF_UNIX;
strncpy(remote.sun_path, g_socket_path, UNIX_PATH_MAX-1);
len = strlen(remote.sun_path) + sizeof(remote.sun_family);
if (connect(stats_socket_fd, (struct sockaddr *)&remote, len) == -1) {
	syslog(3, "stats socket '%s' connection failed: %s", g_socket_path, strerror(errno));
	close(stats_socket_fd);
	stats_socket_fd = -1;
}
*/
return;
}

void reinit_log_file(int signum)
{
char logfile[PATH_MAX];

if(logfile_fd >= 0) {
	close(logfile_fd);
}
snprintf(logfile,sizeof(logfile),"%s/%s/iostats/%d",g_cache_dir,g_rewrite_dir,getpid());
if((logfile_fd = creat(logfile,S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH|S_IWOTH)) == -1) {
		return;
	}
write(logfile_fd,INITSTR,strlen(INITSTR));
return;
}


int copy_file_contents(const char *srcfile,int oldfd, const struct stat *srcstat,const char *dstfile)
{
int fd_src = -1, fd_dst = -1;

unsigned char copybuf[COPY_BUFFER_SIZE];
struct utimbuf ftime;

if(oldfd < 0) {
	if((fd_src = real_open(srcfile,O_RDONLY)) == -1) {
		return -1;
	}
} else
	fd_src = oldfd;

if((fd_dst = creat(dstfile,srcstat->st_mode)) == -1) {
		if(!copy_recursive_dirs(srcfile,dstfile)) {
			if((fd_dst = creat(dstfile,srcstat->st_mode)) == -1)
				return -1;
		} else 
			return -1;
} 

if(srcstat->st_size < g_maxfilesize) {
	ssize_t total_read = 0,n_read = 0,n_write = 0;
	while(total_read < srcstat->st_size) {
		n_read = read(fd_src,copybuf,sizeof(copybuf));
		if(n_read > 0) 
			n_write = write(fd_dst,copybuf,n_read);

		if(n_read == -1 || n_write != n_read) {
			close(fd_src);
			ftruncate(fd_dst,0);
			close(fd_dst);
			/* e.g. ENOSPC */
			return -2;
		}
		total_read += n_read;
	}
}
close(fd_dst);
if(oldfd < 0) 
	close(fd_src);
if(oldfd > 0)
	lseek(fd_src,0,SEEK_SET);

ftime.actime = srcstat->st_atime;
ftime.modtime = srcstat->st_mtime;
utime(dstfile,&ftime);
return 0;
}

void create_whiteout(char *wpath)
{
char *dup_wpath;
int ret = 0;
if(strstr(wpath,".whiteout"))
	return;
strcat(wpath,".whiteout");
dup_wpath = strdup(wpath);
if(!dup_wpath)
	return;
create_path(dup_wpath);
/* create whiteout */
ret = creat(dup_wpath,S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
if(ret >= 0)
	close(ret);
free(dup_wpath);
return;
}

int create_path(char *path)
{
char *path_bn;
struct stat spath;

path_bn = strrchr(path,'/');

*path_bn = '\0';

if(real_xstat(1,path,&spath) == -1) {
	int id;
        id = create_path(path);
	if(id == -1 || mkdir(path,0755))
		return -1;
	*path_bn = '/';
	return id;
}
*path_bn = '/';
return spath.st_uid;
}


int copy_recursive_dirs(const char *oldpath, const char *cachepath)
{
char *dup_path = NULL;
char *dup_cachepath = NULL;
int ret = 0;

dup_path = strdup(oldpath);
if(!dup_path)
	return -1;

dup_cachepath = strdup(cachepath);
if(!dup_cachepath) {
	free(dup_path);
	return -1;
}
ret = copy_recursive_exec(dup_path,dup_cachepath);
free(dup_path);
free(dup_cachepath);
if(ret != -2)
	copy_dir_entries(oldpath,cachepath);
return ret;
}

int copy_dir_entries(const char *oldpath, const char *cachepath)
{
DIR *dfd;
struct dirent *dirp = NULL;
struct dirent *prev_dirp = NULL;
struct stat htstat;
char full_path[PATH_MAX];
char new_oldpath[PATH_MAX];
char *full_name;
dfd = real_opendir(oldpath);
if(dfd) {
	prev_dirp =  (struct dirent *) malloc(offsetof(struct dirent, d_name) + fpathconf(dirfd(dfd),_PC_NAME_MAX) + 1);
	if(!prev_dirp)
		goto cleanup;

	while(1) {
		if(readdir_r(dfd,prev_dirp,&dirp))
			goto cleanup;
		if(!dirp)
			goto cleanup;

		full_name = dirp->d_name;
		if(!strcmp(full_name,".") || !strcmp(full_name,".."))
			continue;
		snprintf(full_path,PATH_MAX,"%s/%s", cachepath, full_name);
		if(dirp->d_type == DT_REG || dirp->d_type == DT_UNKNOWN) {
			int ofd;
			ofd = creat(full_path,S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
			if(ofd >= 0) 
				close(ofd);
			if(!strcmp(full_name,".htaccess")) {
				snprintf(new_oldpath,PATH_MAX,"%s/.htaccess", oldpath);
				if(!real_xstat(1,new_oldpath,&htstat)) {
					copy_file_contents(new_oldpath,-1,&htstat,full_path);
				}
			}
		}
		if(dirp->d_type == DT_DIR) {
			(void) mkdir(full_path,040711);
			snprintf(new_oldpath,PATH_MAX,"%s/%s", oldpath, full_name);
			/*copy_dir_entries(new_oldpath,full_path);*/
		}
	}
}
cleanup:
	free(prev_dirp);
	closedir(dfd);
	return 0;
}

int copy_recursive_exec(const char *oldpath, const char *cachepath)
{
char *path_bn;
char *cache_bn;
char *cpath = (char *) cachepath;
char *ppath = (char *) oldpath;
struct stat pathstat;
struct stat cachepathstat;
struct utimbuf dtime;

if(real_xstat(1,cpath,&cachepathstat) == -1) {
	path_bn = strrchr(ppath,'/');
	cache_bn = strrchr(cpath,'/');
if(real_xstat(1,ppath,&pathstat) == -1) {
		return -2;
		/* part of source path is missing. this should not happend */
	}
	*path_bn = '\0';
	*cache_bn = '\0';

        copy_recursive_exec(ppath,cpath);

	*path_bn = '/';
	*cache_bn = '/';
	if(S_ISDIR(pathstat.st_mode)) {
		if(!mkdir(cpath,040711)) {
			dtime.actime = pathstat.st_atime;
			dtime.modtime = pathstat.st_mtime;
			utime(cpath,&dtime);
		}
	} else
		return -2;
}
return 0;
}


void copy_entry(const char *oldpath,int oldfd,const struct stat *oldstat, const char *cachepath)
{
	if(!oldstat)
		return;
	/*
	if(getgid() == APACHE_DEFAULT_GROUP) {
		parent_dir = strstr(cachepath,".htaccess");
		if(parent_dir) {
			parent_dir--;
			*parent_dir = '\0';
			if(real_access(cachepath,F_OK) == -1) {
				*parent_dir = '/';
				return;
			}
				*parent_dir = '/';
		} else    
			return;
	}
	*/
	if(strncmp(oldpath,cachepath + strlen(g_cache_dir), strlen(g_cache_dir) )){
		/* name of file/dir is not same */
			return;
	}

	if(S_ISDIR(oldstat->st_mode)) {
		copy_recursive_dirs(oldpath,cachepath);
	}
	
	if(S_ISREG(oldstat->st_mode)) 
		copy_file_contents(oldpath,oldfd,oldstat,cachepath);

	return;
}

int whiteout_check(const char *argpath)
{
int ret = 0;
char cachepath[PATH_MAX];
snprintf((char *) &cachepath,sizeof(cachepath),"%s%s.whiteout",g_cache_dir,argpath);
ret = real_access(cachepath,F_OK);
if(!ret) {
	return 0;
}
if(ret == -1 && errno == ENOTDIR) {
	return 0;
}
return ret;	
}
