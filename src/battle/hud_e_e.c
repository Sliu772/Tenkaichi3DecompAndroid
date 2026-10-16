#include "common.h"
#include "sys/math3d.h"
#include "sys/rigid.h"
#include "battle/stg_d.h"

/*
 * Stage rigid bodies, the two list helpers in front of src/battle/stg_d.c (0x22FC40-0x22FD10). They belong to
 * that file (same module, same list idiom); see battle/stg_d.h.
 */

/* Number of nodes of a list.
 * The walk goes through a copy of the parameter: taking the PARAMETER's address is what makes ptr32.py give it 4 bytes
 * (port/tools/ptr32.py:97-119), while every declaration of this function (stg_d.c) keeps the outer pointer ordinary.
 * On iOS the two forms are not the same value (4-byte offsets vs 64-bit addresses), so they must agree. A local copy
 * is the same code with agreeing types. */
s32 StgRigidList_Count(StgRigidNode *head) {
    StgRigidNode *cur = head;
    StgRigidNode **link = &cur;
    s32 n = 0;

    while (*link != NULL) {
        n++;
        link = &(*link)->next;
    }
    return n;
}

/* Takes a node: the first of the free list, else the next unused one of the pool (at most 127 are handed out);
   NULL when the pool is used up. The node's resting word is cleared. */
StgRigidNode *StgRigid_AllocNode(void) {
    StgRigidNode *n;

    if (gStgRigid->free != NULL) {
        n = gStgRigid->free;
        gStgRigid->free = n->next;
    } else if (gStgRigid->used < STG_RIGID_MAX - 1) {
        n = &gStgRigid->nodes[gStgRigid->used++];
    } else {
        return NULL;
    }
    n->resting = 0;
    return n;
}
