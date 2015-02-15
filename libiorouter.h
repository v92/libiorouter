#ifndef LIBIOROUTER_H
#define LIBIOROUTER_H
#define SOCK_PATH "/var/run/libiorouter.sock"

#define CACHEDIR "/run"

#define REWRITEDIR "/nfsmnt"

#define COPY_BUFFER_SIZE 65536
#define MAXFILESIZE 10485760

#define APACHE_DEFAULT_GROUP 1003
#define DIRPBUF_SIZE 4096


#define INITSTR "libiorouter has been inicialized.\n"

#define REDIRCHECK(funcstr,func,...) \
	if(!path || !io_on_off || strstr(path,".snapshot")) { \
                LOGSEND(L_STATS, "CALL %s %s",funcstr,path); \
                return func(__VA_ARGS__); \
        } \
	if(strncmp(path,REWRITEDIR,strlen(REWRITEDIR))) { \
                LOGSEND(L_STATS, "CALL %s %s",funcstr,path); \
                ret = func(__VA_ARGS__); \
		free(path); \
		return ret; \
        } \

#define L_JOURNAL 1
#define L_STATS 2

#define LOGSEND(attr,fmt,...) \
	{ \
	int n_msg; \
	int log_attr = (attr); \
	char msg[PATH_MAX]; \
	struct timeval sec; \
	gettimeofday(&sec,NULL); \
	n_msg = snprintf((char *) &msg,sizeof(msg),"%ld.%ld "fmt"\n",sec.tv_sec,sec.tv_usec,__VA_ARGS__); \
	if(stats_socket_fd != -1 && (log_attr & L_STATS)) \
		(void) send(stats_socket_fd, msg, n_msg, 0); \
	if(logfile_fd != -1 && (log_attr & L_JOURNAL)) \
		(void) write(logfile_fd,msg,n_msg); \
	}

/*int (*real_fchownat)(int dirfd, const char *path, uid_t owner, gid_t group, int flags);j
int (*real_fchmodat)(int dirfd, const char *path, mode_t mode, int flags);*/
int create_path(char *path);
int copy_recursive_dirs(const char *oldpath,const char *cachepath);
int copy_recursive_exec(const char *oldpath,const char *cachepath);
int copy_dir_entries(const char *oldpath, const char *cachepath);
void reread_conf(int signum);
void read_conf_file(char *logserver);
void init_log_socket(char *logserver);
char *normalize_path(const char * , size_t);
char *libio_realpath(const char *);
char *libio_realpath_chk(const char *, char *, size_t);
void copy_entry(const char *oldpath,int oldfd,const struct stat *oldstat, const char *cachepath);
void create_whiteout(char *wpath);
void reinit_log_file(int signum);
void socket_init(void);
int whiteout_check(const char *argpath);
#endif
