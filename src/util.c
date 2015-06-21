#include "util.h"

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

