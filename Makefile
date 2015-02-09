all:
	gcc libiorouter.c iohooks.c normalize_path.c -ansi -Wall -DRWCACHE -g -ldl -shared -fPIC -o libiorouter_rw.so
#	gcc normalize_path.c libiorouter.c -ansi -Wall -g -ldl -shared -fPIC -o libiorouter_ro.so

clean:
	-rm libiorouter_.so
