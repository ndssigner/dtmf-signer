/* Where libspeex's init functions take their memory from: a caller's buffer
 * instead of the heap, so a decoder lives inside a ds_speex. Single
 * threaded: spx_arena_use sets the buffer for the init calls that follow,
 * and spx_arena_use(NULL, 0) ends it. Nothing is ever freed one by one. */
#ifndef SPX_ARENA_H
#define SPX_ARENA_H

#include <stddef.h>

void spx_arena_use(void *mem, size_t len);

/* Zeroed, 8-byte aligned; NULL when the buffer is missing or full. */
void *spx_arena_alloc(size_t size);

#endif
