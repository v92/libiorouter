#ifndef LIBIOROUTER_H
#define LIBIOROUTER_H
/* edit defaults here */
#define DEFAULT_SOCK_PATH "/var/run/libiorouter.sock"
#define DEFAULT_CACHEDIR "/run"
#define DEFAULT_REWRITEDIR "/nfsmnt"
#define DEFAULT_MAXFILESIZE 10485760
#define DEFAULT_WHITELIST "wsapache24-phpx|wsapache22-phpx|wsapache22-static|wsapache24-static|hhvm|proftpd"

#define COPY_BUFFER_SIZE 65536

#define APACHE_DEFAULT_GROUP 1003
#define DIRPBUF_SIZE 4096


#define INITSTR "libiorouter has been inicialized.\n"

#define REDIRCHECK(funcstr,func,...) \
	if(!path || strstr(path,".snapshot")) { \
                LOGSEND(L_STATS, "CALL %s %s",funcstr,path); \
                return func(__VA_ARGS__); \
        } \
	if(strncmp(path,g_rewrite_dir,strlen(g_rewrite_dir))) { \
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
		if(logstats_fd != -1 && (log_attr & L_STATS)) \
			(void) write(logstats_fd,msg,n_msg); \
		if(logjournal_fd != -1 && (log_attr & L_JOURNAL)) \
			(void) write(logjournal_fd,msg,n_msg); \
	}

int create_path(char *path);
int copy_recursive_dirs(const char *oldpath,const char *cachepath);
int copy_recursive_exec(const char *oldpath,const char *cachepath);
int copy_dir_entries(const char *oldpath, const char *cachepath);
void reread_conf(int signum);
void read_conf_file(char *logserver);
void init_log_socket(char *logserver);
char *normalize_path(const char *);
char *normalize_pathat(int,const char *);
char *libio_realpath(const char *);
char *libio_realpath_chk(const char *, char *, size_t);
void copy_entry(const char *oldpath,int oldfd,const struct stat *oldstat, const char *cachepath);
void create_whiteout(char *wpath);
void reinit_log_file(int signum);
void socket_init(void);
int whiteout_check(const char *argpath);
#endif
