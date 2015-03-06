/*
Copyright (C) 2013 arnaud576875 @ stackoverflow.com

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/


#define _GNU_SOURCE /* memrchr() */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <fcntl.h>

extern char chroot_path[PATH_MAX];

/* lstat-less realpath version */
char * normalize_pathat(int dirfd,const char * src)
{
        char * res;
        size_t res_len;
        size_t src_len;
	size_t pwd_len;
	
        const char * ptr;
        const char * end;
        const char * next;
	char pwd[PATH_MAX];
	
	if(!src)	
		return NULL;

        src_len = strlen(src);
        ptr = src;
        end = &src[src_len];

        if (src[0] != '/') {
		if(dirfd == AT_FDCWD) {
			if(chroot_path[0] != '\0') 
				strncpy(pwd,chroot_path,strlen(chroot_path));	

			if (getcwd(pwd + strlen(chroot_path), PATH_MAX - strlen(chroot_path) - 1) == NULL) 
				return NULL;
			pwd_len = strlen(pwd);
		} else {
			char lnkpth[PATH_MAX];
			snprintf(lnkpth,PATH_MAX,"/proc/%d/fd/%d",getpid(),dirfd);
			if((pwd_len = readlink(lnkpth,pwd,PATH_MAX - 1)) == -1) 
				return NULL;
			lnkpth[pwd_len] = '\0';
		}

                res = malloc(pwd_len + 1 + src_len + 1);
                memcpy(res, pwd, pwd_len);
                res_len = pwd_len;
        } else {
                res = malloc((src_len > 0 ? src_len : 1) + 1);
                res_len = 0;
        }

        for (ptr = src; ptr < end; ptr=next+1) {
                size_t len;
                next = memchr(ptr, '/', end-ptr);
                if (next == NULL) {
                        next = end;
                }
                len = next-ptr;
                switch(len) {
                case 2:
                        if (ptr[0] == '.' && ptr[1] == '.') {
                                const char * slash = memrchr(res, '/', res_len);
                                if (slash != NULL) {
                                        res_len = slash - res;
                                }
                                continue;
                        }
                        break;
                case 1:
                        if (ptr[0] == '.') {
                                continue;

                        }
                        break;
                case 0:
                        continue;
                }
                res[res_len++] = '/';
                memcpy(&res[res_len], ptr, len);
                res_len += len;
        }

        if (res_len == 0) {
                res[res_len++] = '/';
        }
        res[res_len] = '\0';
        return res;
}

char * normalize_path(const char * src)
{
	return normalize_pathat(AT_FDCWD,src);
}

char *libio_realpath(const char * src)
{

char *rpath = normalize_path(src);
if(rpath) 
	return rpath;
return NULL;
}


char *libio_realpath_chk(const char *buf, char *resolved, size_t resolvedlen)
{

resolved = normalize_path(buf);
if(resolved) 
	return resolved;
return NULL;
}

