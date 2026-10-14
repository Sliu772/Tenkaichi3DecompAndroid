#ifndef COMMON_H
#define COMMON_H

#include "include_asm.h"
#include "types.h"

#define NULL 0

/* A game address (32 bits) out of a pointer. On iOS the game's 4-byte addresses are offsets from the Mach-O header
   (port/tools/p32rebase.py), and a plain (u32)pointer of a pointer the compiler holds as an ordinary 64-bit one (an
   argument, a local) is the low half of the real address instead: always the difference to the header. 0 stays 0. */
#ifdef __APPLE__
extern char _mh_execute_header[] __attribute__((visibility("hidden")));
#define GAME_ADDR(p) ((p) == NULL ? 0u : (unsigned int)((unsigned char *)(p) - (unsigned char *)_mh_execute_header))
#else
#define GAME_ADDR(p) ((unsigned int)(p))
#endif

#endif
