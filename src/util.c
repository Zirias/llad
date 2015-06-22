#include "util.h"

#include <string.h>

#include "daemon.h"

void *
lladAlloc(size_t size)
{
    void *alloc = malloc(size);
    if (!alloc)
    {
	daemon_perror("Fatal: Could not allocate memory");
	exit(EXIT_FAILURE);
    }
    return alloc;
}

char *
lladCloneString(const char *s)
{
    char *dst = lladAlloc(strlen(s)+1);
    strcpy(dst, s);
    return dst;
}
