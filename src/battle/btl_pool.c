#include "common.h"
#include "battle/btl_pool.h"
#include "sys/heap.h"

extern void *memset(void *dst, s32 c, u32 n);

extern BtlPool *gBtlPool;
/* Written by BtlPool_Init and BtlPool_SetInitArg, never read. */
extern s32 gBtlPoolInitArg;

/* The arena block is reached through a pointer to the field, not as a plain member: after a store to it
 * the compiler reloads gBtlPool, which a direct `gBtlPool->arenaMem = x` does not reproduce. */
#define ARENA_MEM (*&gBtlPool->arenaMem)

#ifdef PORT
extern void Port_DiagNote(const char *what, long a, long b);
#define POOL_EXTRA_MAX 256
static void *sPoolExtra[BTL_POOL_SLOT_COUNT][POOL_EXTRA_MAX]; /* heap blocks that stood in for an arena that was full */
static s32 sPoolExtraN[BTL_POOL_SLOT_COUNT];
#endif

s32 BtlPool_CalcArenaSize(void);
void BtlPool_CarveArenas(void);
u8 *BtlPool_InitSlot(BtlPoolSlot *slot, s32 size, u8 *mem);

/* Allocates the pool manager and the memory behind every arena slot. */
void BtlPool_Init(s32 arg) {
    s32 size;

    gBtlPoolInitArg = arg;
    gBtlPool = Heap_Alloc(sizeof(BtlPool), 0x20, 0, HEAP_ANY);
    memset(gBtlPool, 0, sizeof(BtlPool));
    gBtlPool->arenaMask = 0x3FE;
    size = BtlPool_CalcArenaSize();
    if (size > 0) {
        ARENA_MEM = Heap_Alloc(size, 0x20, 0, HEAP_ANY);
        memset(ARENA_MEM, 0, size);
        BtlPool_CarveArenas();
    }
}

/* Frees the arena memory and the pool manager. */
void BtlPool_Term(void) {
    if (ARENA_MEM != NULL) {
        Heap_Free(ARENA_MEM);
        ARENA_MEM = NULL;
    }
    if (gBtlPool != NULL) {
        Heap_Free(gBtlPool);
        gBtlPool = NULL;
    }
}

/* Allocates size bytes (rounded up to 32) from a slot: bump allocation for an arena, the heap otherwise. */
void *BtlPool_Alloc(s32 slot, s32 size) {
    s32 aligned = BtlPool_AlignUp32(size);
    BtlPoolSlot *s = &gBtlPool->slots[slot];
    void *ptr;

    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(slot)) {
#ifdef PORT
        /* An arena has no capacity check in the original: past its end the bump pointer runs into the next arena and then
           into whatever follows the block in the heap (display lists, file buffers). Here an allocation that does not fit
           comes from the heap instead (freed by BtlPool_Reset) and the first overflows are logged. */
        if (s->base != NULL && (s32)(s->cur - s->base) + aligned > s->size) {
            void *extra = Heap_Alloc(size, 0x20, 0, HEAP_ANY);
            Port_DiagNote("pool: arena overflow, slot*1000+size/32", (long)slot * 1000 + aligned / 32, (long)s->used);
            if (extra != NULL) {
                memset(extra, 0, size);
                if (sPoolExtraN[slot] < POOL_EXTRA_MAX) {
                    sPoolExtra[slot][sPoolExtraN[slot]++] = extra;
                }
            }
            s->used += aligned;
            return extra;
        }
#endif
        ptr = s->cur;
        s->cur += aligned;
        s->used += aligned;
    } else {
        s->used += aligned;
        ptr = Heap_Alloc(size, 0x20, 0, HEAP_ANY);
    }
    return ptr;
}

#ifdef PORT
/* 1 when `p` lies inside the arena memory of the slot. */
int BtlPool_Contains(s32 slot, const void *p) {
    BtlPoolSlot *s;

    if (gBtlPool == NULL || p == NULL || slot < 0 || slot >= BTL_POOL_SLOT_COUNT) {
        return 0;
    }
    s = &gBtlPool->slots[slot];
    return s->base != NULL && (const u8 *)p >= s->base && (const u8 *)p < s->base + s->size;
}
#endif

/* Frees a pointer from a slot; a no-op for arena slots, which are only released by BtlPool_Reset. */
void BtlPool_Free(s32 slot, void *ptr) {
    if (!(gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(slot))) {
        if (ptr != NULL) {
            Heap_Free(ptr);
        }
    }
}

/* Rewinds an arena slot to empty. */
void BtlPool_Reset(s32 slot) {
    BtlPoolSlot *s = &gBtlPool->slots[slot];

    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(slot)) {
#ifdef PORT
        {
            s32 i;
            for (i = 0; i < sPoolExtraN[slot]; i++) {
                Heap_Free(sPoolExtra[slot][i]);
            }
            sPoolExtraN[slot] = 0;
        }
#endif
        s->cur = s->base;
        s->used = 0;
    }
}

/* Selects the slot that BtlPool_GetCurrent reports. */
void BtlPool_SetCurrent(s32 slot) {
    gBtlPool->current = slot;
}

/* Returns the slot selected by BtlPool_SetCurrent. */
s32 BtlPool_GetCurrent(void) {
    return gBtlPool->current;
}

/* Stores the value BtlPool_Init also records (never read back). */
void BtlPool_SetInitArg(s32 arg) {
    gBtlPoolInitArg = arg;
}

/* Returns the total size of all enabled arena slots. */
s32 BtlPool_CalcArenaSize(void) {
    s32 total = 0;

    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(0)) {
        total = BtlPool_AlignUp32(0xA000);
    }
    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(1)) {
        total += BtlPool_AlignUp32(0x113000);
    }
    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(2)) {
        total += BtlPool_AlignUp32(0x1E000);
    }
    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(3)) {
        total += BtlPool_AlignUp32(0x1400);
    }
    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(4)) {
        total += BtlPool_AlignUp32(0x19000);
    }
    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(5)) {
        total += BtlPool_AlignUp32(0x19000);
    }
    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(6)) {
        total += BtlPool_AlignUp32(0x400);
    }
    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(7)) {
        total += BtlPool_AlignUp32(0x19000);
    }
    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(8)) {
        total += BtlPool_AlignUp32(0x19000);
    }
    return total;
}

/* Hands each enabled arena slot its piece of arenaMem. Slots 5 and 8 test the bit of slots 4 and 7 (original bug). */
void BtlPool_CarveArenas(void) {
    u8 *mem = gBtlPool->arenaMem;

    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(0)) {
        mem = BtlPool_InitSlot(&gBtlPool->slots[0], BtlPool_AlignUp32(0xA000), mem);
    }
    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(1)) {
        mem = BtlPool_InitSlot(&gBtlPool->slots[1], BtlPool_AlignUp32(0x113000), mem);
    }
    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(2)) {
        mem = BtlPool_InitSlot(&gBtlPool->slots[2], BtlPool_AlignUp32(0x1E000), mem);
    }
    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(3)) {
        mem = BtlPool_InitSlot(&gBtlPool->slots[3], BtlPool_AlignUp32(0x1400), mem);
    }
    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(4)) {
        mem = BtlPool_InitSlot(&gBtlPool->slots[4], BtlPool_AlignUp32(0x19000), mem);
    }
    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(4)) {
        mem = BtlPool_InitSlot(&gBtlPool->slots[5], BtlPool_AlignUp32(0x19000), mem);
    }
    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(6)) {
        mem = BtlPool_InitSlot(&gBtlPool->slots[6], BtlPool_AlignUp32(0x400), mem);
    }
    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(7)) {
        mem = BtlPool_InitSlot(&gBtlPool->slots[7], BtlPool_AlignUp32(0x19000), mem);
    }
    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(7)) {
        mem = BtlPool_InitSlot(&gBtlPool->slots[8], BtlPool_AlignUp32(0x19000), mem);
    }
}

/* Points a slot at its arena memory and returns the address just past it. */
u8 *BtlPool_InitSlot(BtlPoolSlot *slot, s32 size, u8 *mem) {
    slot->size = size;
    slot->base = mem;
    slot->cur = mem;
    return mem + size;
}

/* Rounds a size up to a multiple of 32. */
s32 BtlPool_AlignUp32(s32 size) {
    if (size % 32) {
        size = size / 32 * 32 + 32;
    }
    return size;
}
