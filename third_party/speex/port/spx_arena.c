/* See spx_arena.h. */
#include <stdint.h>
#include <string.h>

#include "spx_arena.h"

static uint8_t *arena_at;
static size_t arena_left;

void spx_arena_use(void *mem, size_t len)
{
    arena_at = (uint8_t *)mem;
    arena_left = mem ? len : 0;
    /* The buffer itself may start unaligned: skip to an 8-byte boundary. */
    size_t pad = (size_t)(-(uintptr_t)arena_at & 7u);
    if (pad > arena_left) {
        pad = arena_left;
    }
    arena_at += pad;
    arena_left -= pad;
}

void *spx_arena_alloc(size_t size)
{
    size_t rounded = (size + 7u) & ~(size_t)7u;
    if (!arena_at || rounded < size || rounded > arena_left) {
        return NULL;
    }
    void *p = arena_at;
    memset(p, 0, rounded);
    arena_at += rounded;
    arena_left -= rounded;
    return p;
}
