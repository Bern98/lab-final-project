#include "sfiles.h"

char *get_file_extension(char *filename)
{
	char *s = strstr(filename, ".dat");
	if(s == NULL) return "\0";
	return s;
}