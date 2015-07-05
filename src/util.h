#ifndef LLAD_UTIL_H
#define LLAD_UTIL_H

/** common utility functions
 * @file
 */

#include <stdlib.h>

/** Allocate memory.
 * Wrapper around malloc that immediately fails on out of memory conditions.
 * @param size the size of the memory block to allocate
 * @returns a pointer to the newly allocated memory
 */
void *lladAlloc(size_t size);

/** Clone a string.
 * This works like strcpy, except it uses lladAlloc() for allocating memory.
 * @param s the string to clone
 * @returns pointer to the cloned string
 */
char *lladCloneString(const char *s);

#endif
