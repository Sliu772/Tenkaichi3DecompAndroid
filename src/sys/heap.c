#include "common.h"
#include "sys/heap.h"

extern void *malloc(u32 size);
s32 Mem_Align4Pad(u32 value);
s32 Mem_Align4Rem(u32 value);

extern HeapBlock *gHeapStart[HEAP_COUNT];
extern HeapBlock *gHeapEnd[HEAP_COUNT];
extern s32 gHeapSize[HEAP_COUNT];
/* Written by the setters below and cleared by Heap_Init, but never read: leftovers of a debug feature. */
extern s32 gHeapDebugValue;
extern u8 gHeapDebugFlag;
extern s32 gHeapDebugUnused;
extern u8 D_3BE71C[];

#ifdef __APPLE__
/* iOS: the game's 4-byte addresses are OFFSETS from the image's Mach-O header (port/tools/p32rebase.py). A plain (u32)pointer
   is not one: depending on how the compiler holds the pointer it is the low half of the real address, which carries the
   ASLR slide and changes with every run (the heap's data addresses came out displaced by the slide: log 21 / 22). The
   difference to the header is the offset however the pointer is held. 0 stays 0. */
extern char _mh_execute_header[] __attribute__((visibility("hidden")));
#define PTR_ADDR(p) ((p) == NULL ? 0u : (u32)((u8 *)(p) - (u8 *)_mh_execute_header))
#else
#define PTR_ADDR(p) ((u32)(p))
#endif
extern s32 gHeapUnk70[HEAP_COUNT];
extern s32 gHeapUnk78[HEAP_COUNT];

void Heap_ReportBadFree(void);

u32 Heap_BlockData(HeapBlock *block) {
    return PTR_ADDR(block) + sizeof(HeapBlock);
}

void Heap_InitFreeBlock(HeapBlock *block, s32 size) {
    block->magic = HEAP_MAGIC;
    block->align = 0x20;
    block->dataSize = size - 0x20;
    block->used = 0;
    block->fromTail = 0;
    block->size = size;
    block->data = 0;
    *(s32 *)((u8 *)block + size - 4) = size;
}

void Heap_InitUsedBlock(HeapBlock *block, s32 size, s32 dataSize, u32 align, s32 fromTail) {
    u32 data = Heap_BlockData(block);

    block->magic = HEAP_MAGIC;
    block->used = 1;
    block->fromTail = fromTail;
    block->align = align;
    block->size = size;
    block->data = data + (align - data % align) % align;
    block->dataSize = dataSize;
    *(s32 *)((u8 *)block + size - 4) = size;
}

HeapBlock *Heap_PrevBlock(HeapBlock *block) {
    HeapBlock *prev = NULL;

    if (block != NULL) {
        if (block != gHeapStart[0] && block != gHeapStart[1]) {
            prev = (HeapBlock *)((u8 *)block - *(s32 *)((u8 *)block - 4));
        }
    }
    return prev;
}

HeapBlock *Heap_NextBlock(HeapBlock *block) {
    HeapBlock *next = NULL;

    if (block != NULL) {
        block = (HeapBlock *)((u8 *)block + block->size);
        if (block != gHeapEnd[0] && block != gHeapEnd[1]) {
            next = block;
        }
    }
    return next;
}

void Heap_Create(void) {
#ifdef __APPLE__
    /* iOS: (u32)D_3BE71C is the low half of the symbol's real address (it changes with ASLR); the heap start is the number */
    s32 size = 0x1EFB000 - 0x3BE71C;
#else
    s32 size = 0x1EFB000 - (u32)D_3BE71C;
#endif
    HeapBlock *start;

    size += Mem_Align4Pad(size);
    start = malloc(size);
    gHeapSize[0] = size;
    gHeapEnd[0] = (HeapBlock *)((u8 *)start + size);
    gHeapStart[0] = start;
    Heap_InitFreeBlock(start, size);
}

HeapBlock *Heap_FindBlock(u32 data) {
    HeapBlock *found = NULL;
    HeapBlock *block;
    s32 i;

    for (i = 0; i < HEAP_COUNT; i++) {
        for (block = gHeapStart[i]; block != NULL; block = Heap_NextBlock(block)) {
            if (block->used == 1 && block->data == data) {
                found = block;
                break;
            }
        }
    }
    return found;
}

HeapBlock *Heap_FindFreeFromHead(s32 heap, s32 size);

HeapBlock *Heap_SearchFromHead(s32 size, s32 heap) {
    HeapBlock *block = NULL;
    s32 i;

    if (heap != HEAP_ANY) {
        block = Heap_FindFreeFromHead(heap, size);
    } else {
        for (i = 0; i < HEAP_COUNT; i++) {
            block = Heap_FindFreeFromHead(i, size);
            if (block != NULL) {
                break;
            }
        }
    }
    return block;
}

HeapBlock *Heap_FindFreeFromHead(s32 heap, s32 size) {
    HeapBlock *found = NULL;
    HeapBlock *block;

    for (block = gHeapStart[heap]; block != NULL; block = Heap_NextBlock(block)) {
        if (block->used == 0 && block->size >= size) {
            found = block;
            break;
        }
    }
    return found;
}

HeapBlock *Heap_FindFreeFromTail(s32 heap, s32 size);

HeapBlock *Heap_SearchFromTail(s32 size, s32 heap) {
    HeapBlock *block = NULL;
    s32 i;

    if (heap != HEAP_ANY) {
        block = Heap_FindFreeFromTail(heap, size);
    } else {
        for (i = HEAP_COUNT - 1; i >= 0; i--) {
            block = Heap_FindFreeFromTail(i, size);
            if (block != NULL) {
                break;
            }
        }
    }
    return block;
}

HeapBlock *Heap_FindFreeFromTail(s32 heap, s32 size) {
    HeapBlock *found = NULL;
    HeapBlock *block;

    for (block = Heap_PrevBlock(gHeapEnd[heap]); block != NULL; block = Heap_PrevBlock(block)) {
        if (block->used == 0 && block->size >= size) {
            found = block;
            break;
        }
    }
    return found;
}

HeapBlock *Heap_AllocBlock(s32 size, u32 align, s32 fromTail, s32 heap) {
    HeapBlock *block;
    HeapBlock *free;
    s32 need;

    if (size <= 0) {
        return NULL;
    }
    need = size + align + 0x24;
    if (fromTail == 0) {
        block = Heap_SearchFromHead(need, heap);
    } else {
        block = Heap_SearchFromTail(need, heap);
    }
    if (block == NULL) {
        return NULL;
    }
    if (block->dataSize - need > 0x80) {
        if (fromTail == 0) {
            need += Mem_Align4Pad(PTR_ADDR(block) + need);
            Heap_InitFreeBlock((HeapBlock *)((u8 *)block + need), block->size - need);
        } else {
            free = block;
            need += Mem_Align4Rem(PTR_ADDR(block) + (block->size - need));
            block = (HeapBlock *)((u8 *)block + (free->size - need));
            free->dataSize -= need;
            free->size -= need;
            *(s32 *)((u8 *)free + free->size - 4) = free->size;
        }
    } else {
        need = block->size;
    }
    Heap_InitUsedBlock(block, need, size, align, fromTail);
    return block;
}

void Heap_FreeBlock(HeapBlock *block) {
    HeapBlock *prev;
    HeapBlock *next;
    s32 merge = 0;

    Heap_InitFreeBlock(block, block->size);
    prev = Heap_PrevBlock(block);
    next = Heap_NextBlock(block);
    if (prev != NULL) {
        merge = prev->used == 0;
    }
    if (next != NULL && next->used == 0) {
        merge |= 2;
    }
    switch (merge) {
    case 0:
        break;
    case 1:
        prev->size += block->size;
        prev->dataSize += block->size;
        *(s32 *)((u8 *)prev + prev->size - 4) = prev->size;
        break;
    case 2:
        block->size += next->size;
        block->dataSize += next->size;
        *(s32 *)((u8 *)block + block->size - 4) = block->size;
        break;
    case 3:
        prev->size += block->size + next->size;
        prev->dataSize += block->size + next->size;
        *(s32 *)((u8 *)prev + prev->size - 4) = prev->size;
        break;
    }
}

void Heap_Init(void) {
    gHeapDebugValue = 0;
    gHeapDebugFlag = 0;
    gHeapDebugUnused = 0;
    Heap_Create();
}

void *Heap_Alloc(s32 size, u32 align, s32 fromTail, s32 heap) {
    HeapBlock *block = Heap_AllocBlock(size, align, fromTail, heap);
    void *data = NULL;

    if (block != NULL) {
        data = (void *)block->data;
    }
#ifdef PORT
    {
        /* diagnostic (iOS bring-up): the first allocations (block address, data field, pointer handed out: the data has to
           lie 0x20..0x5F behind its block) and, when an allocation fails, what the heap holds. */
        extern void Port_HeapAllocDiag(s32 size, u32 block, u32 field, u32 ret);
        extern void Port_HeapAllocFail(s32 size, s32 align, s32 heap, s32 freeTotal, s32 largest, s32 nfree, s32 nused);
        static s32 sAllocDiagN;
        HeapBlock *b;
        s32 i, nfree, nused, freeTotal, largest;

        if (block != NULL && sAllocDiagN < 8) {
            sAllocDiagN++;
            Port_HeapAllocDiag(size, PTR_ADDR(block), block->data, PTR_ADDR(data));
        }
        if (block == NULL && size > 0) {
            nfree = 0;
            nused = 0;
            freeTotal = 0;
            largest = 0;
            for (i = 0; i < HEAP_COUNT; i++) {
                for (b = gHeapStart[i]; b != NULL; b = Heap_NextBlock(b)) {
                    if (b->used == 1) {
                        nused++;
                    } else {
                        nfree++;
                        freeTotal += b->size;
                        if (b->size > largest) {
                            largest = b->size;
                        }
                    }
                }
            }
            Port_HeapAllocFail(size, (s32)align, heap, freeTotal, largest, nfree, nused);
        }
    }
#endif
    return data;
}

void Heap_Free(void *ptr) {
    HeapBlock *block;

#ifdef PORT
    {
        /* PC build: a block that is not the heap's (a large stage buffer, battle_load.c) goes back to the port */
        extern int Port_GameBigFree(unsigned addr);
        if (Port_GameBigFree(PTR_ADDR(ptr))) {
            return;
        }
    }
#endif
    block = Heap_FindBlock(PTR_ADDR(ptr));

    if (block == NULL) {
#ifdef PORT
        {
            /* diagnostic (iOS bring-up): why the block is not there. Per heap: the first block, how many blocks the
               walk finds, the first used block's data address, the last block and whether it ends at gHeapEnd. */
            extern void Port_HeapDiag(u32 ptr, s32 heap, u32 start, u32 end, s32 blocks, s32 used, u32 firstData, u32 lastBlock, s32 lastSize, u32 lastMagic);
            HeapBlock *b;
            HeapBlock *last;
            s32 i, n, nused;
            u32 firstData;

            for (i = 0; i < HEAP_COUNT; i++) {
                n = 0;
                nused = 0;
                firstData = 0;
                last = NULL;
                for (b = gHeapStart[i]; b != NULL && n < 100000; b = Heap_NextBlock(b)) {
                    if (b->used == 1) {
                        if (nused == 0) {
                            firstData = b->data;
                        }
                        nused++;
                    }
                    last = b;
                    n++;
                }
                Port_HeapDiag(PTR_ADDR(ptr), i, (u32)gHeapStart[i], (u32)gHeapEnd[i], n, nused, firstData, (u32)last,
                              last != NULL ? last->size : 0, last != NULL ? last->magic : 0);
            }
            {
                /* Which block holds the pointer: as given (k 0) and with the ASLR slide taken off (k 1). */
                extern u32 Port_DiagSlide(void);
                extern void Port_HeapDiag2(s32 k, u32 ptr, u32 blk, s32 used, u32 data, s32 size);
                u32 cand[2];
                s32 k, hit;

                cand[0] = PTR_ADDR(ptr);
                cand[1] = PTR_ADDR(ptr) - Port_DiagSlide();
                for (k = 0; k < 2; k++) {
                    hit = 0;
                    for (i = 0; i < HEAP_COUNT; i++) {
                        for (b = gHeapStart[i]; b != NULL; b = Heap_NextBlock(b)) {
                            if ((u32)b <= cand[k] && cand[k] < (u32)b + (u32)b->size) {
                                Port_HeapDiag2(k, cand[k], (u32)b, b->used, b->data, b->size);
                                hit = 1;
                            }
                        }
                    }
                    if (!hit) {
                        Port_HeapDiag2(k, cand[k], 0, 0, 0, 0);
                    }
                }
            }
        }
        /* a pointer that is not a block: do not go on with block == NULL (Heap_FreeBlock(NULL) crashes) */
        return;
#endif
        Heap_ReportBadFree();
    }
    Heap_FreeBlock(block);
}

void Heap_SetDebugValue(s32 value);
void Heap_SetDebugFlag(u8 flag);

void Heap_SetDebug(s32 value, u8 flag) {
    Heap_SetDebugValue(value);
    Heap_SetDebugFlag(flag);
}

void Heap_SetDebugValue(s32 value) {
    gHeapDebugValue = value;
}

void Heap_SetDebugFlag(u8 flag) {
    gHeapDebugFlag = flag;
}

/* Total bytes available in free blocks, across every heap. */
s32 Heap_GetFreeSize(void) {
    s32 total = 0;
    HeapBlock *block;
    s32 i;

    for (i = 0; i < HEAP_COUNT; i++) {
        for (block = gHeapStart[i]; block != NULL; block = Heap_NextBlock(block)) {
            if (block->used == 0) {
                total += block->dataSize;
            }
        }
    }
    return total;
}

/* Total bytes requested by live allocations, across every heap. */
s32 Heap_GetUsedSize(void) {
    s32 total = 0;
    HeapBlock *block;
    s32 i;

    for (i = 0; i < HEAP_COUNT; i++) {
        for (block = gHeapStart[i]; block != NULL; block = Heap_NextBlock(block)) {
            if (block->used == 1) {
                total += block->dataSize;
            }
        }
    }
    return total;
}

/* Size of the largest single free block. */
u32 Heap_GetLargestFree(void) {
    u32 largest = 0;
    HeapBlock *block;
    s32 i;

    for (i = 0; i < HEAP_COUNT; i++) {
        for (block = gHeapStart[i]; block != NULL; block = Heap_NextBlock(block)) {
            if (block->used == 0) {
                if (largest < (u32)block->dataSize) {
                    largest = block->dataSize;
                }
            }
        }
    }
    return largest;
}

s32 Heap_GetTotalSize(void) {
    return gHeapSize[0] + gHeapSize[1];
}

/* Called by Heap_Free for a pointer that is not a live block. The report itself was compiled out. */
void Heap_ReportBadFree(void) {
}

HeapBlock *Heap_GetStart(s32 heap) {
    return gHeapStart[heap];
}

HeapBlock *Heap_GetEnd(s32 heap) {
    return gHeapEnd[heap];
}

s32 Heap_GetSize(s32 heap) {
    return gHeapSize[heap];
}

s32 Heap_GetUnk70(s32 heap) {
    return gHeapUnk70[heap];
}

s32 Heap_GetUnk78(s32 heap) {
    return gHeapUnk78[heap];
}
