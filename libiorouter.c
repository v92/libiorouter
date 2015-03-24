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
#include <sys/stat.h>
#include <sys/time.h>
#include <errno.h>
#include <utime.h>
#include <unistd.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <syslog.h>
#include <ctype.h>
#include <regex.h>

#include "libiorouter.h"
#include "iohooks.h"

#define HOOK(iotohook,ioptr) \
	if(!ioptr) { ioptr = dlsym(RTLD_NEXT,iotohook); }



int debug_on_off = 1;	/* 0 - debug off, 1 - debug on */
int trace_on_off = 1;	/* 0 - trace off, 1 - trace on */
int io_on_off = 0;	/* 0 - io routing off, 1 - io routing on */

int logstats_fd = -1;
int logjournal_fd = -1;
int stats_socket_fd = -1;
struct sockaddr_in udps;

char *g_socket_path = NULL;
char *g_cache_dir = NULL;
size_t g_maxfilesize = 0;
char *g_rewrite_dir = NULL;
char *g_whitelist_regex = NULL;
char chroot_path[PATH_MAX];

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

void set_io_by_comm(const char *comm)
{
char *tmp = strdup(g_whitelist_regex);
char *prev_tmp;
char *token;
if(!tmp)
	return;
prev_tmp = tmp;
while((token = strtok_r(tmp, "|", &tmp))) {
	if(!strncmp(comm,token,strlen(comm))) {
		syslog(3, "libiorouter: command '%s' found in whitelist '%s'",comm,g_whitelist_regex);
		io_on_off = 1;	
		free(prev_tmp);
		return;
	}
}
syslog(3, "libiorouter: command '%s' not found in whitelist '%s'",comm,g_whitelist_regex);
free(prev_tmp);
return;
}

void init_global_vars(const char *comm)
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

if((tmp = getenv("LIBIOR_WHITELIST_REGEX")) != NULL)
	g_whitelist_regex = strdup(tmp);
else
	g_whitelist_regex = DEFAULT_WHITELIST;

set_io_by_comm(comm);

if((tmp = getenv("LIBIOR_IO")) != NULL) {
	if(!strcmp(tmp,"on"))
		io_on_off = 1;
	if(!strcmp(tmp,"off"))
		io_on_off = 0;
	syslog(3, "libiorouter: LIBIOR_IO env is overriding whitelist option, setting IO routing to '%s'",io_on_off == 1 ? "on": "off" );
}
syslog(3, "libiorouter: stats socket set to '%s'", g_socket_path);
syslog(3, "libiorouter: cache dir set to '%s'", g_cache_dir);
syslog(3, "libiorouter: rewrite dir set to '%s'", g_rewrite_dir);
syslog(3, "libiorouter: max file size set to '%d'", (int) g_maxfilesize);
syslog(3, "libiorouter: IO routing whitelist '%s'", g_whitelist_regex);
syslog(3, "libiorouter: IO routing is %s", io_on_off == 1 ? "on": "off");
return;
}

static void libiorouter_init(void) __attribute__ ((constructor));

static void libiorouter_init(void) 
{
char commpath[PATH_MAX],comm[PATH_MAX];
int commfd,n;
HOOK("open",real_open);
HOOK("bfd_openw",real_bfd_openw);
/*HOOK("fopen",real_fopen);*/
HOOK("creat",real_creat);
HOOK("opendir",real_opendir);
HOOK("chmod",real_chmod);
HOOK("fchmodat",real_fchmodat);
HOOK("chown",real_chown);
HOOK("fchownat",real_fchownat);
HOOK("realpath",real_realpath);
HOOK("link",real_link);
HOOK("symlink",real_symlink);
HOOK("rename",real_rename);
HOOK("renameat",real_renameat);
HOOK("unlink",real_unlink);
HOOK("unlinkat",real_unlinkat);
HOOK("access",real_access);
HOOK("faccessat",real_faccessat);
HOOK("rmdir",real_rmdir);
HOOK("mkdir",real_mkdir);
	
HOOK("__xstat",real_xstat);
HOOK("__xstat64",real_xstat64);
HOOK("fxstatat",real_fxstatat);
HOOK("__realpath_chk",real_realpath_chk);
HOOK("__lxstat",real_lxstat);
HOOK("__lxstat64",real_lxstat64);

HOOK("chroot",real_chroot);

memset(comm,'\0',sizeof(comm));
snprintf(commpath,PATH_MAX,"/proc/%d/comm",getpid());
commfd = real_open(commpath,O_RDONLY);
if(commfd >= 0) {
	n = read(commfd,(char *) &comm,sizeof(comm));
	close(commfd);
	if(n >= 1) {
		comm[n - 1] = '\0';
		syslog(3, "libiorouter: my name is '%s'", comm);
	}
}
init_global_vars(comm);
reinit_log_file(SIGPROF);
signal(SIGTTIN,traceonoff);	
signal(SIGTTOU,debugonoff);	
signal(SIGURG,ioonoff);	
signal(SIGPROF,reinit_log_file);	
return;
}

void reinit_log_file(int signum)
{
char logfile[PATH_MAX];

if(logstats_fd >= 0) {
	close(logstats_fd);
}
snprintf(logfile,sizeof(logfile),"%s/%s/iostats/%d.stats",g_cache_dir,g_rewrite_dir,getpid());
if((logstats_fd = real_creat(logfile,S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH|S_IWOTH)) == -1) {
		return;
	}
write(logstats_fd,INITSTR,strlen(INITSTR));

if(logjournal_fd >= 0) {
	close(logjournal_fd);
}
snprintf(logfile,sizeof(logfile),"%s/%s/iostats/%d.journal",g_cache_dir,g_rewrite_dir,getpid());
if((logjournal_fd = real_creat(logfile,S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH|S_IWOTH)) == -1) {
		return;
	}
write(logjournal_fd,INITSTR,strlen(INITSTR));
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

if((fd_dst = real_creat(dstfile,srcstat->st_mode)) == -1) {
		if(!copy_recursive_dirs(srcfile,dstfile)) {
			if((fd_dst = real_creat(dstfile,srcstat->st_mode)) == -1)
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
ret = real_creat(dup_wpath,S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
if(ret >= 0)
	close(ret);
free(dup_wpath);
return;
}

int create_path(char *path)
{
char *path_bn;
struct stat spath;

if(!path)
	return -1;

path_bn = strrchr(path,'/');

*path_bn = '\0';

if(real_xstat(1,path,&spath) == -1) {
	int id;
	if(S_ISREG(spath.st_mode)) {
		real_unlink(path);
	}
        id = create_path(path);
	if(id == -1 || real_mkdir(path,0755))
		return -1;
	*path_bn = '/';
	return id;
} else {
	if(S_ISREG(spath.st_mode)) {
		real_unlink(path);
		real_mkdir(path,0755);
	}
}
*path_bn = '/';
return 0;
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
		int ofd;
		ofd = real_creat(full_path,S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
		if(ofd >= 0) 
			close(ofd);
		if(!strcmp(full_name,".htaccess")) {
			snprintf(new_oldpath,PATH_MAX,"%s/.htaccess", oldpath);
			if(!real_xstat(1,new_oldpath,&htstat)) {
				copy_file_contents(new_oldpath,-1,&htstat,full_path);
			}
		}
			
		/*
		if(dirp->d_type == DT_DIR) {
			(void) real_mkdir(full_path,040711);
			snprintf(new_oldpath,PATH_MAX,"%s/%s", oldpath, full_name);
			copy_dir_entries(new_oldpath,full_path);
		}
		*/
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
		if(!real_mkdir(cpath,040711)) {
			dtime.actime = pathstat.st_atime;
			dtime.modtime = pathstat.st_mtime;
			utime(cpath,&dtime);
		}
	}
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
char *path = NULL;
snprintf((char *) &cachepath,sizeof(cachepath),"%s%s.whiteout",g_cache_dir,argpath);
ret = real_access(cachepath,F_OK);
if(!ret) {
	return 0;
}
if(ret == -1 && errno == ENOTDIR) {
	path = normalize_path(argpath);
	snprintf((char *) &cachepath,sizeof(cachepath),"%s%s",g_cache_dir,path);
	create_path(cachepath);
	free(path);
	return -1;
}
return ret;	
}
