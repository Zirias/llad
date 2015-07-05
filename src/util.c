#include "util.h"

#include <string.h>
#include <errno.h>

#include "daemon.h"

void *
lladAlloc(size_t size)
{
    void *alloc = malloc(size);
    if (!alloc)
    {
	daemon_printf_level(LEVEL_CRIT,
		"Could not allocate memory: %s", strerror(errno));
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
