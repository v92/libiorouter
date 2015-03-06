/* foo.c */
extern char * normalize_path(const char * src, size_t src_len);

char * normalize_path(const char * src, size_t src_len)
{
	__coverity_panic__();
}
