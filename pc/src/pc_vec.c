/* pc_vec.c — the vector library, in plain C.
 *
 * vec.c is the companion to the matrix sources pc_mtx.c replaces, and is
 * assembly for the same reason: paired-single instructions process two floats
 * at once, which suits three-component vectors awkwardly enough that the SDK
 * hand-wrote them.
 *
 * The same accuracy caveat applies. These are not bit-identical to the
 * originals -- the console's fused multiply-add rounds once where a separate
 * multiply and add round twice -- and they sit under the game's physics. See
 * the note in pc_mtx.c.
 */
#include "pc_sys.h"

#include <dolphin/mtx.h>

void PSVECAdd(Vec* a, Vec* b, Vec* ab)
{
    ab->x = a->x + b->x;
    ab->y = a->y + b->y;
    ab->z = a->z + b->z;
}

void PSVECSubtract(Vec* a, Vec* b, Vec* a_b)
{
    a_b->x = a->x - b->x;
    a_b->y = a->y - b->y;
    a_b->z = a->z - b->z;
}

void PSVECScale(Vec* src, Vec* dst, f32 scale)
{
    dst->x = src->x * scale;
    dst->y = src->y * scale;
    dst->z = src->z * scale;
}

f32 PSVECDotProduct(Vec* a, Vec* b)
{
    return a->x * b->x + a->y * b->y + a->z * b->z;
}

void PSVECCrossProduct(Vec* a, Vec* b, Vec* axb)
{
    /* Through a temporary: callers do pass the same vector as both an input
       and the destination, and writing x first would corrupt the y and z
       terms that still need it. */
    f32 x = a->y * b->z - a->z * b->y;
    f32 y = a->z * b->x - a->x * b->z;
    f32 z = a->x * b->y - a->y * b->x;
    axb->x = x;
    axb->y = y;
    axb->z = z;
}

f32 PSVECMag(Vec* v)
{
    return sqrtf(v->x * v->x + v->y * v->y + v->z * v->z);
}

void PSVECNormalize(Vec* src, Vec* unit)
{
    f32 mag = PSVECMag(src);
    if (mag == 0.0f) {
        unit->x = unit->y = unit->z = 0.0f;
        return;
    }
    mag = 1.0f / mag;
    unit->x = src->x * mag;
    unit->y = src->y * mag;
    unit->z = src->z * mag;
}
