all:
	gcc libiorouter.c iohooks.c normalize_path.c -ansi -Wall -g -ldl -shared -fPIC -o libiorouter.so
test:
	cppcheck --enable=performance,portability,information,missingInclude --std=posix .
clean:
	-rm libiorouter_.so

