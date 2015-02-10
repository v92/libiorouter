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

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

/* lstat-less realpath version */
char * normalize_path(const char * src) {

        char * res;
        size_t res_len;
        size_t src_len;

        const char * ptr = src;
        const char * end = &src[src_len];
        const char * next;
	
	src_len = strlen(src);

        if (src_len == 0 || src[0] != '/') {

                /* relative path */

                char pwd[PATH_MAX];
                size_t pwd_len;

                if (getcwd(pwd, sizeof(pwd)) == NULL) {
                        return NULL;
                }

                pwd_len = strlen(pwd);
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

char *libio_realpath(const char * src, size_t src_len)
{

char *rpath = normalize_path(src,src_len);
if(rpath && !access(rpath,F_OK)) 
	return rpath;
return NULL;
}


char *libio_realpath_chk(const char *buf, char *resolved, size_t resolvedlen)
{

resolved = normalize_path(buf,strlen(buf));
if(resolved && !access(resolved,F_OK)) 
	return resolved;
return NULL;
}

