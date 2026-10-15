#include "common.h"
#include "battle/btl_char_cam_int.h"

#ifdef PORT
extern void Port_DiagNote(const char *what, long a, long b); /* bring-up (iOS): bt3_log.txt */
extern f32 BtlCharApi_GetRadius(s32 objId);
#endif

/* The fighter camera's cut evaluator, 0x1C4F68-0x1C5840: the middle of btl_char_cam.c, still a file of its own (see the
 * note at the top of btl_char_cam.c; the three can be merged now that it matches). It emits two .lit4 words, both 0.001f
 * (0x2FD08C, 0x2FD090). */

/* Evaluates the running camera cut: refreshes the node positions, interpolates eye / target / look-at and the
   three angles by the cut's progress, derives the rotation, raises the cut's flags and counts its timer down.
   The masked node id is written out at each of the two calls (`node & CHRCUT_NODE_MASK`): a variable for it
   shares the node's register and frees one for a hoisted &tmp0, which the original does not have. */
void ChrCam_CalcCut(ChrCamChr *chr, Vec4 *eye, Vec4 *rot, Vec4 *target) {
    Vec4 look;
    Vec4 dir;
    Vec4 tmp0;
    Vec4 tmp1;
    Vec4 tmp2;
    Vec4 tmp3;
    ChrCamCut *cut = &chr->cut;
    s32 frozen;
    ChrCam *cam;
    s32 refreshed;
    s32 usedOpp;
    s32 usedMid;
    f32 t;
    s32 node;
    s32 other;
    s32 id;
    f32 eps;
    f32 yaw;
    f32 pitch;
    f32 dist;
    f32 len;
    f32 limit;
    f32 a;

    frozen = BtlChar_TestFlag(chr, 0xB4);
    cam = &chr->cam;
    t = 0.0f;
    refreshed = 0;
    usedOpp = 0;
    usedMid = 0;
    if (cut->total > 0) {
        t = 1.0f - (f32)cut->timer / (f32)cut->total;
    }
#ifdef PORT
    {
        static s32 sCuts; /* fix73: s32 casts (a long cast calls __fixsfdi, which the port lacks and aborts); what each camera cut asks for (transformations, finishers), to find why it sits too close */

        if (sCuts < 24 && cut->total > 0 && cut->timer == cut->total) {
            sCuts++;
            Port_DiagNote("camera cut: frames, flags", cut->total, cut->flags);
            Port_DiagNote("camera cut: distance x1000, change x1000", (s32)(cut->valC * 1000.0f), (s32)(cut->valCDelta * 1000.0f));
            Port_DiagNote("camera cut: eye node, look node", cut->unk88, cut->unk90);
            Port_DiagNote("camera cut: fighter height x1000, object id", (s32)(BtlCharApi_GetHeight(chr->objId) * 1000.0f), chr->objId);
        }
    }
#endif

    node = cut->unk88;
    if (node >= 0) {
        other = 0;
        if (node & CHRCUT_NODE_OPP) {
            usedOpp = 1;
            id = BtlOpp_GetObjId(chr);
            other = 1;
        } else if (node & CHRCUT_NODE_MID) {
            id = -1;
            usedMid = 1;
        } else {
            id = chr->objId;
        }
        if (id < 0) {
            BtlCharApi_GetNodePos(BtlChar_Get(0)->objId, node & CHRCUT_NODE_MASK, &tmp0);
            BtlCharApi_GetNodePos(BtlChar_Get(1)->objId, node & CHRCUT_NODE_MASK, &tmp1);
            Vec4_Lerp(&cut->vecA, &tmp0, &tmp1, 0.5f);
        } else if (frozen == 0 || other == 0) {
            BtlCharApi_GetNodePos(id, cut->unk88 & CHRCUT_NODE_MASK, &cut->vecA);
        }
        refreshed = 1;
    }

    node = cut->unk8C;
    if (node >= 0) {
        if (cut->unk88 == node) {
            Vec4_SetZeroW1(&cut->vecADelta);
        } else {
            other = 0;
            if (node & CHRCUT_NODE_OPP) {
                usedOpp = 1;
                id = BtlOpp_GetObjId(chr);
                other = 1;
            } else if (node & CHRCUT_NODE_MID) {
                id = -1;
                usedMid = 1;
            } else {
                id = chr->objId;
            }
            if (id < 0) {
                BtlCharApi_GetNodePos(BtlChar_Get(0)->objId, node & CHRCUT_NODE_MASK, &tmp2);
                BtlCharApi_GetNodePos(BtlChar_Get(1)->objId, node & CHRCUT_NODE_MASK, &tmp3);
                Vec4_Lerp(&tmp0, &tmp2, &tmp3, 0.5f);
            } else if (frozen == 0 || other == 0) {
                BtlCharApi_GetNodePos(id, cut->unk8C & CHRCUT_NODE_MASK, &tmp0);
            }
            Vec4_Sub(&cut->vecADelta, &tmp0, &cut->vecA);
        }
        refreshed = 1;
    }

    node = cut->unk90;
    if (node >= 0) {
        other = 0;
        if (node & CHRCUT_NODE_OPP) {
            id = BtlOpp_GetObjId(chr);
            other = 1;
        } else if (node & CHRCUT_NODE_MID) {
            id = -1;
        } else {
            id = chr->objId;
        }
        if (id < 0) {
            BtlCharApi_GetNodePos(BtlChar_Get(0)->objId, node & CHRCUT_NODE_MASK, &tmp0);
            BtlCharApi_GetNodePos(BtlChar_Get(1)->objId, node & CHRCUT_NODE_MASK, &tmp1);
            Vec4_Lerp(&cut->vecC, &tmp0, &tmp1, 0.5f);
        } else if (frozen == 0 || other == 0) {
            BtlCharApi_GetNodePos(id, node & CHRCUT_NODE_MASK, &cut->vecC);
        }
    }

    node = cut->unk94;
    if (node >= 0) {
        if (cut->unk90 == node) {
            Vec4_SetZeroW1(&cut->vecCDelta);
        } else {
            other = 0;
            if (node & CHRCUT_NODE_OPP) {
                id = BtlOpp_GetObjId(chr);
                other = 1;
            } else if (node & CHRCUT_NODE_MID) {
                id = -1;
            } else {
                id = chr->objId;
            }
            if (id < 0) {
                BtlCharApi_GetNodePos(BtlChar_Get(0)->objId, node & CHRCUT_NODE_MASK, &tmp1);
                BtlCharApi_GetNodePos(BtlChar_Get(1)->objId, node & CHRCUT_NODE_MASK, &tmp2);
                Vec4_Lerp(&tmp0, &tmp1, &tmp2, 0.5f);
            } else if (frozen == 0 || other == 0) {
                BtlCharApi_GetNodePos(id, node & CHRCUT_NODE_MASK, &tmp0);
            }
            Vec4_Sub(&cut->vecCDelta, &tmp0, &cut->vecC);
        }
    }
    if (refreshed) {
        if (usedOpp) {
            Vec4_Copy(&cut->vecB, *(Vec4 **)((u8 *)BtlOpp_GetObj(chr) + 0xFA0));
        } else if (usedMid) {
            Vec4_Copy(&tmp0, *(Vec4 **)((u8 *)BtlOpp_GetObj(chr) + 0xFA0));
            Vec4_Copy(&tmp1, *(Vec4 **)((u8 *)BtlChar_GetObj(chr) + 0xFA0));
            Vec4_Lerp(&cut->vecB, &tmp0, &tmp1, 0.5f);
        } else {
            Vec4_Copy(&cut->vecB, *(Vec4 **)((u8 *)BtlChar_GetObj(chr) + 0xFA0));
        }
        Vec4_SetZeroW1(&cut->vecBDelta);
    }

    eps = 0.001f;
    Vec4_Scale(eye, &cut->vecADelta, t);
    Vec4_Add(eye, &cut->vecA, eye);
    Vec4_Scale(target, &cut->vecBDelta, t);
    Vec4_Add(target, &cut->vecB, target);
    Vec4_Scale(&look, &cut->vecCDelta, t);
    Vec4_Add(&look, &cut->vecC, &look);
    yaw = cut->valA + cut->valADelta * t;
    pitch = -(cut->valB + cut->valBDelta * t);
    dist = cut->valC + cut->valCDelta * t;
    dir.x = Mathf_Cos(pitch) * Mathf_Sin(yaw);
    dir.y = Mathf_Sin(pitch);
    dir.z = Mathf_Cos(pitch) * Mathf_Cos(yaw);
    Vec4_Scale(&dir, &dir, dist);
#ifdef PORT
    {
        /* fix74: the cut's own-fighter nodes (eye base node A, look node C) are read from the model's node matrices. In the
           transformation closing cut (cut 6) the log showed the eye 26..115 units from the body while the cut asks for
           8.7 (limit 7.4), i.e. the node position does not follow the fighter right after the model swap. When a node
           of the fighter's own is farther than 1.5 body heights from the body, the body position stands in for it
           (eye base at the body's middle, look at the head height); the angles and the distance are the cut's own. */
        f32 h = BtlCharApi_GetHeight(chr->objId);
        static s32 sFix;
        static s32 sTrace2;

        if (h >= 1.0f && h < 1000.0f && cut->unk0 == 0) {
            f32 bx = chr->body.pos.x;
            f32 by = chr->body.pos.y;
            f32 bz = chr->body.pos.z;
            f32 ex = eye->x - bx;
            f32 ez = eye->z - bz;
            f32 lx = look.x - bx;
            f32 lz = look.z - bz;
            f32 de = __builtin_sqrtf(ex * ex + ez * ez);
            f32 dl = __builtin_sqrtf(lx * lx + lz * lz);

            if (sTrace2 < 16 && cut->unk4 == 6 && (cut->timer % 3) == 0) {
                sTrace2++;
                Port_DiagNote("camera cut 6: eye base x z x1000", (s32)(eye->x * 1000.0f), (s32)(eye->z * 1000.0f));
                Port_DiagNote("camera cut 6: body x z x1000", (s32)(bx * 1000.0f), (s32)(bz * 1000.0f));
                Port_DiagNote("camera cut 6: look x z x1000", (s32)(look.x * 1000.0f), (s32)(look.z * 1000.0f));
            }
            if (cut->unk88 >= 0 && !(cut->unk88 & (CHRCUT_NODE_OPP | CHRCUT_NODE_MID)) && de > h * 1.5f) {
                eye->x = bx;
                eye->y = by - h * 0.5f; /* y is up-negative */
                eye->z = bz;
                if (sFix < 20) {
                    sFix++;
                    Port_DiagNote("camera cut: eye base node far from body, body used; distance x1000, height x1000", (s32)(de * 1000.0f), (s32)(h * 1000.0f));
                }
            }
            if (cut->unk90 >= 0 && !(cut->unk90 & (CHRCUT_NODE_OPP | CHRCUT_NODE_MID)) && dl > h * 1.5f) {
                look.x = bx;
                look.y = by - h * 0.8f;
                look.z = bz;
                if (sFix < 20) {
                    sFix++;
                    Port_DiagNote("camera cut: look node far from body, body used; distance x1000, height x1000", (s32)(dl * 1000.0f), (s32)(h * 1000.0f));
                }
            }
        }
    }
#endif
    Vec4_Sub(eye, eye, &dir);
#ifdef PORT
    {
        /* Cut cameras only (the fight camera is ChrCam_CalcFree / CalcLockOn and is not touched): the eye is the node A
           position pulled back by `dist` along the cut's angles, and while the model is rebuilt or the node is on the far
           side of the body that point can fall inside the fighter's own body (the camera is then seen going through it).
           It is moved out, horizontally, to the body's radius plus a tenth of its height; the target is not changed, so
           the cut keeps looking where it looked. */
        f32 radius = BtlCharApi_GetRadius(chr->objId);
        f32 height = BtlCharApi_GetHeight(chr->objId);
        f32 minR = radius + height * 0.1f;
        f32 dx = eye->x - chr->body.pos.x;
        f32 dz = eye->z - chr->body.pos.z;
        f32 d = __builtin_sqrtf(dx * dx + dz * dz);
        f32 mid = chr->body.pos.y - height * 0.5f; /* y is up-negative */
        static s32 sPushed;
        static s32 sTrace;

        if (height >= 1.0f && height < 1000.0f && dist > 0.5f && __builtin_fabsf(eye->y - mid) < height * 0.6f) {
            if (sTrace < 60 && chr->cut.unk0 == 0 && chr->cut.unk4 == 6 && (cut->timer % 3) == 0) {
                sTrace++;
                Port_DiagNote("camera cut 6: eye to body (horizontal) x1000, limit x1000", (s32)(d * 1000.0f), (s32)(minR * 1000.0f));
            }
            if (d < minR) {
                if (d < 0.01f) {
                    dx = -Mathf_Sin(yaw);
                    dz = -Mathf_Cos(yaw);
                    d = 1.0f;
                }
                eye->x = chr->body.pos.x + dx / d * minR;
                eye->z = chr->body.pos.z + dz / d * minR;
                if (sPushed < 40) {
                    sPushed++;
                    Port_DiagNote("camera clearance: eye was inside the body, distance x1000, limit x1000", (s32)(d * 1000.0f), (s32)(minR * 1000.0f));
                }
            }
        }
    }
#endif
    Vec4_SetZero(rot);
    Vec4_Sub(&dir, &look, eye);
    len = Vec3_Length(&dir);
    if (eps < len) {
        Vec4_Scale(&dir, &dir, 1.0f / len);
        Vec3_Clamp(&dir, &dir, -1.0f, 1.0f);
        rot->x = Mathf_Asin(-dir.y);
        if (__builtin_fabsf(dir.x) < eps && __builtin_fabsf(dir.z) < eps) {
            rot->y = 0.0f;
        } else {
            rot->y = atan2f(dir.x, dir.z);
        }
    }

    if (cut->flags & CHRCUT_F_SNAP_RUN) {
        BtlChar_SetFlag(chr, 0xCD);
    }
    if (cut->flags & CHRCUT_F_RATE_RUN) {
        BtlChar_SetFlag(chr, 0xD1);
    }
    if (cut->flags & CHRCUT_F_PRIORITY) {
        BtlChar_SetFlag(chr, 0xD3);
    }
    if (cut->timer > 0) {
        cut->timer--;
        if (cut->timer == 0 && !(cut->flags & CHRCUT_F_HOLD)) {
            if (cut->flags & CHRCUT_F_SNAP_END) {
                BtlChar_SetFlag(chr, 0xCE);
            }
            ChrCam_EndCut(chr);
        }
    }
    if (cut->flags & CHRCUT_F_SET_SIDE) {
        Vec4_Sub(&tmp2, &BtlChar_GetPos(chr)->pos, &cam->eye);
        if (0.001f < __builtin_fabsf(tmp2.x) || 0.001f < __builtin_fabsf(tmp2.z)) {
            limit = ChrCam_GetSideLimit(chr);
            a = atan2f(tmp2.x, tmp2.z);
            chr->cam.side = BtlUtil_ClampF(-BtlUtil_WrapAngle(a - BtlChar_GetPos(chr)->yaw), -limit, limit);
        }
    }
    BtlChar_SetFlag(chr, 0xD4);
}
