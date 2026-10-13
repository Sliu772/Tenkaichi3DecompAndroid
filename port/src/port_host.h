/* PORT_HOST: a variable of a port file that is NOT part of the game's state although the file's other variables
   are (port/tools/make_state.py lists those for saving and restoring): handles of the operating system (open
   files) and the like. Restoring an earlier state must not bring an old handle back. Such variables go into a
   section of their own, which the list leaves out. */
#ifndef PORT_HOST_H
#define PORT_HOST_H
#ifdef __APPLE__
#define PORT_HOST __attribute__((section("__DATA,__porthst"))) /* Mach-O wants "segment,section" */
#else
#define PORT_HOST __attribute__((section(".porthst")))
#endif

/* The game's 4-byte addresses and the port's own (host) code.
   Everywhere else the game's address is the number the host's pointer holds. On iOS it is an OFFSET from the image's
   Mach-O header (port/tools/p32rebase.py: iOS reserves the whole low 4 GB), so a host function that is handed an
   address as an integer (a DMA tag's next address, a pointer read out of the game's memory) turns it into a pointer
   with Port_A2P and the other way round with Port_P2A. 0 stays 0 / NULL in both directions. */
#include <stdint.h>
#ifdef __APPLE__
#include <mach-o/ldsyms.h>
#define PORT_BASE ((uintptr_t)&_mh_execute_header)
#else
#define PORT_BASE ((uintptr_t)0)
#endif
static inline void *Port_A2P(uint32_t a) {
    return a != 0 ? (void *)(PORT_BASE + (uintptr_t)a) : (void *)0;
}
static inline uint32_t Port_P2A(const void *p) {
    return p != (const void *)0 ? (uint32_t)((uintptr_t)p - PORT_BASE) : 0;
}
#endif
