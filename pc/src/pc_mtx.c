/* pc_mtx.c — the matrix library, in plain C.
 *
 * mtx.c, psmtx.c, mtxvec.c and vec.c are written in the Gekko's paired-single
 * instructions, which process two floats at a time. That is the whole reason
 * they are assembly, and it is why they do not follow us off PowerPC.
 *
 * A Mtx is f32[3][4]: a 3x4 affine transform whose implied fourth row is
 * (0 0 0 1). Every routine below is the ordinary form of what the assembly
 * computes two lanes at a time.
 *
 * Results are not bit-identical to the originals. Paired-single arithmetic is
 * IEEE single precision like x86's, but the console's fused multiply-add
 * rounds once where a separate multiply and add round twice, so the low bit
 * can differ. Melee is deterministic and these routines sit under its physics,
 * so anything depending on frame-exact behaviour wants the accumulation order
 * matched before it is trusted -- the same caveat that applies to atanf in
 * pc_libc.c.
 */
#include "pc_sys.h"

#include <dolphin/mtx.h>

void PSMTXIdentity(Mtx m)
{
    m[0][0] = 1.0f; m[0][1] = 0.0f; m[0][2] = 0.0f; m[0][3] = 0.0f;
    m[1][0] = 0.0f; m[1][1] = 1.0f; m[1][2] = 0.0f; m[1][3] = 0.0f;
    m[2][0] = 0.0f; m[2][1] = 0.0f; m[2][2] = 1.0f; m[2][3] = 0.0f;
}

void PSMTXCopy(Mtx src, Mtx dst)
{
    int i, j;
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 4; j++) {
            dst[i][j] = src[i][j];
        }
    }
}

/* mAB = mA * mB, with the implied (0 0 0 1) fourth row of mB folded into the
   translation column. Written through a temporary so aliasing any of the three
   arguments is safe, which callers do rely on. */
void PSMTXConcat(Mtx mA, Mtx mB, Mtx mAB)
{
    Mtx t;
    int i;
    for (i = 0; i < 3; i++) {
        t[i][0] = mA[i][0] * mB[0][0] + mA[i][1] * mB[1][0] + mA[i][2] * mB[2][0];
        t[i][1] = mA[i][0] * mB[0][1] + mA[i][1] * mB[1][1] + mA[i][2] * mB[2][1];
        t[i][2] = mA[i][0] * mB[0][2] + mA[i][1] * mB[1][2] + mA[i][2] * mB[2][2];
        t[i][3] = mA[i][0] * mB[0][3] + mA[i][1] * mB[1][3] + mA[i][2] * mB[2][3] +
                  mA[i][3];
    }
    PSMTXCopy(t, mAB);
}

/* Transposes the 3x3 rotation and clears the translation, which is what the
   SDK documents -- it is not a full inverse. */
void PSMTXTranspose(Mtx src, Mtx xPose)
{
    Mtx t;
    t[0][0] = src[0][0]; t[0][1] = src[1][0]; t[0][2] = src[2][0]; t[0][3] = 0.0f;
    t[1][0] = src[0][1]; t[1][1] = src[1][1]; t[1][2] = src[2][1]; t[1][3] = 0.0f;
    t[2][0] = src[0][2]; t[2][1] = src[1][2]; t[2][2] = src[2][2]; t[2][3] = 0.0f;
    PSMTXCopy(t, xPose);
}

u32 PSMTXInverse(Mtx src, Mtx inv)
{
    Mtx t;
    f32 det;
    f32 idet;

    det = src[0][0] * (src[1][1] * src[2][2] - src[1][2] * src[2][1]) -
          src[0][1] * (src[1][0] * src[2][2] - src[1][2] * src[2][0]) +
          src[0][2] * (src[1][0] * src[2][1] - src[1][1] * src[2][0]);

    if (det == 0.0f) {
        return 0;
    }
    idet = 1.0f / det;

    t[0][0] = (src[1][1] * src[2][2] - src[1][2] * src[2][1]) * idet;
    t[0][1] = (src[0][2] * src[2][1] - src[0][1] * src[2][2]) * idet;
    t[0][2] = (src[0][1] * src[1][2] - src[0][2] * src[1][1]) * idet;
    t[1][0] = (src[1][2] * src[2][0] - src[1][0] * src[2][2]) * idet;
    t[1][1] = (src[0][0] * src[2][2] - src[0][2] * src[2][0]) * idet;
    t[1][2] = (src[0][2] * src[1][0] - src[0][0] * src[1][2]) * idet;
    t[2][0] = (src[1][0] * src[2][1] - src[1][1] * src[2][0]) * idet;
    t[2][1] = (src[0][1] * src[2][0] - src[0][0] * src[2][1]) * idet;
    t[2][2] = (src[0][0] * src[1][1] - src[0][1] * src[1][0]) * idet;

    /* The inverse translation is -(R^-1 * t). */
    t[0][3] = -(t[0][0] * src[0][3] + t[0][1] * src[1][3] + t[0][2] * src[2][3]);
    t[1][3] = -(t[1][0] * src[0][3] + t[1][1] * src[1][3] + t[1][2] * src[2][3]);
    t[2][3] = -(t[2][0] * src[0][3] + t[2][1] * src[1][3] + t[2][2] * src[2][3]);

    PSMTXCopy(t, inv);
    return 1;
}

u32 PSMTXInvXpose(Mtx src, Mtx invX)
{
    Mtx inv;
    if (!PSMTXInverse(src, inv)) {
        return 0;
    }
    PSMTXTranspose(inv, invX);
    return 1;
}

void PSMTXTrans(Mtx m, f32 xT, f32 yT, f32 zT)
{
    PSMTXIdentity(m);
    m[0][3] = xT;
    m[1][3] = yT;
    m[2][3] = zT;
}

void PSMTXScale(Mtx m, f32 xS, f32 yS, f32 zS)
{
    PSMTXIdentity(m);
    m[0][0] = xS;
    m[1][1] = yS;
    m[2][2] = zS;
}

/* Rotation about a principal axis, given the sine and cosine directly. */
void PSMTXRotTrig(Mtx m, char axis, f32 sinA, f32 cosA)
{
    PSMTXIdentity(m);
    switch (axis) {
    case 'x': case 'X':
        m[1][1] = cosA;  m[1][2] = -sinA;
        m[2][1] = sinA;  m[2][2] = cosA;
        break;
    case 'y': case 'Y':
        m[0][0] = cosA;  m[0][2] = sinA;
        m[2][0] = -sinA; m[2][2] = cosA;
        break;
    default: /* 'z' */
        m[0][0] = cosA;  m[0][1] = -sinA;
        m[1][0] = sinA;  m[1][1] = cosA;
        break;
    }
}

void MTXRotRad(Mtx m, char axis, f32 rad)
{
    PSMTXRotTrig(m, axis, sinf(rad), cosf(rad));
}

/* Rodrigues' rotation about an arbitrary axis. */
void PSMTXRotAxisRad(Mtx m, Vec* axis, f32 rad)
{
    f32 s = sinf(rad);
    f32 c = cosf(rad);
    f32 t = 1.0f - c;
    f32 len = sqrtf(axis->x * axis->x + axis->y * axis->y + axis->z * axis->z);
    f32 x, y, z;

    if (len == 0.0f) {
        PSMTXIdentity(m);
        return;
    }
    x = axis->x / len;
    y = axis->y / len;
    z = axis->z / len;

    m[0][0] = t * x * x + c;      m[0][1] = t * x * y - s * z;
    m[0][2] = t * x * z + s * y;  m[0][3] = 0.0f;
    m[1][0] = t * x * y + s * z;  m[1][1] = t * y * y + c;
    m[1][2] = t * y * z - s * x;  m[1][3] = 0.0f;
    m[2][0] = t * x * z - s * y;  m[2][1] = t * y * z + s * x;
    m[2][2] = t * z * z + c;      m[2][3] = 0.0f;
}

void PSMTXQuat(Mtx m, QuaternionPtr q)
{
    f32 x = q->x, y = q->y, z = q->z, w = q->w;
    f32 n = x * x + y * y + z * z + w * w;
    f32 s = (n > 0.0f) ? (2.0f / n) : 0.0f;
    f32 xs = x * s,  ys = y * s,  zs = z * s;
    f32 wx = w * xs, wy = w * ys, wz = w * zs;
    f32 xx = x * xs, xy = x * ys, xz = x * zs;
    f32 yy = y * ys, yz = y * zs, zz = z * zs;

    m[0][0] = 1.0f - (yy + zz); m[0][1] = xy - wz;          m[0][2] = xz + wy;
    m[1][0] = xy + wz;          m[1][1] = 1.0f - (xx + zz); m[1][2] = yz - wx;
    m[2][0] = xz - wy;          m[2][1] = yz + wx;          m[2][2] = 1.0f - (xx + yy);
    m[0][3] = m[1][3] = m[2][3] = 0.0f;
}

/* Full affine transform, translation included. */
void PSMTXMultVec(Mtx m, Vec* src, Vec* dst)
{
    f32 x = src->x, y = src->y, z = src->z;
    dst->x = m[0][0] * x + m[0][1] * y + m[0][2] * z + m[0][3];
    dst->y = m[1][0] * x + m[1][1] * y + m[1][2] * z + m[1][3];
    dst->z = m[2][0] * x + m[2][1] * y + m[2][2] * z + m[2][3];
}

/* Rotation and scale only -- "SR" -- for directions rather than positions. */
void PSMTXMultVecSR(Mtx m, Vec* src, Vec* dst)
{
    f32 x = src->x, y = src->y, z = src->z;
    dst->x = m[0][0] * x + m[0][1] * y + m[0][2] * z;
    dst->y = m[1][0] * x + m[1][1] * y + m[1][2] * z;
    dst->z = m[2][0] * x + m[2][1] * y + m[2][2] * z;
}

/* Right-handed view matrix: the camera looks down -Z with `up` righted against
   the view direction. */
void C_MTXLookAt(Mtx m, Point3dPtr camPos, VecPtr camUp, Point3dPtr target)
{
    Vec fwd, side, up;
    f32 len;

    fwd.x = camPos->x - target->x;
    fwd.y = camPos->y - target->y;
    fwd.z = camPos->z - target->z;
    len = sqrtf(fwd.x * fwd.x + fwd.y * fwd.y + fwd.z * fwd.z);
    if (len != 0.0f) {
        fwd.x /= len; fwd.y /= len; fwd.z /= len;
    }

    side.x = camUp->y * fwd.z - camUp->z * fwd.y;
    side.y = camUp->z * fwd.x - camUp->x * fwd.z;
    side.z = camUp->x * fwd.y - camUp->y * fwd.x;
    len = sqrtf(side.x * side.x + side.y * side.y + side.z * side.z);
    if (len != 0.0f) {
        side.x /= len; side.y /= len; side.z /= len;
    }

    up.x = fwd.y * side.z - fwd.z * side.y;
    up.y = fwd.z * side.x - fwd.x * side.z;
    up.z = fwd.x * side.y - fwd.y * side.x;

    m[0][0] = side.x; m[0][1] = side.y; m[0][2] = side.z;
    m[0][3] = -(side.x * camPos->x + side.y * camPos->y + side.z * camPos->z);
    m[1][0] = up.x;   m[1][1] = up.y;   m[1][2] = up.z;
    m[1][3] = -(up.x * camPos->x + up.y * camPos->y + up.z * camPos->z);
    m[2][0] = fwd.x;  m[2][1] = fwd.y;  m[2][2] = fwd.z;
    m[2][3] = -(fwd.x * camPos->x + fwd.y * camPos->y + fwd.z * camPos->z);
}

void MTXLookAt(Mtx m, Vec* camPos, Vec* camUp, Vec* target)
{
    C_MTXLookAt(m, camPos, camUp, target);
}

/* The light-space projections build a texture matrix: a projection followed by
   the bias that maps clip space [-1,1] onto texture space [0,1], scaled. */
void MTXLightFrustum(Mtx m, f32 t, f32 b, f32 l, f32 r, f32 n, f32 scaleS,
                     f32 scaleT, f32 transS, f32 transT)
{
    f32 tmp = 1.0f / (r - l);
    m[0][0] = ((2.0f * n) * tmp) * scaleS;
    m[0][1] = 0.0f;
    m[0][2] = (((r + l) * tmp) * scaleS) - transS;
    m[0][3] = 0.0f;

    tmp = 1.0f / (t - b);
    m[1][0] = 0.0f;
    m[1][1] = ((2.0f * n) * tmp) * scaleT;
    m[1][2] = (((t + b) * tmp) * scaleT) - transT;
    m[1][3] = 0.0f;

    m[2][0] = 0.0f;
    m[2][1] = 0.0f;
    m[2][2] = -1.0f;
    m[2][3] = 0.0f;
}

void MTXLightPerspective(Mtx m, f32 fovY, f32 aspect, f32 scaleS, f32 scaleT,
                         f32 transS, f32 transT)
{
    f32 angle = fovY * 0.5f * 0.01745329252f; /* degrees to radians */
    f32 cot = cosf(angle) / sinf(angle);

    m[0][0] = (cot / aspect) * scaleS;
    m[0][1] = 0.0f;
    m[0][2] = -transS;
    m[0][3] = 0.0f;

    m[1][0] = 0.0f;
    m[1][1] = cot * scaleT;
    m[1][2] = -transT;
    m[1][3] = 0.0f;

    m[2][0] = 0.0f;
    m[2][1] = 0.0f;
    m[2][2] = -1.0f;
    m[2][3] = 0.0f;
}

void MTXLightOrtho(Mtx m, f32 t, f32 b, f32 l, f32 r, f32 scaleS, f32 scaleT,
                   f32 transS, f32 transT)
{
    f32 tmp = 1.0f / (r - l);
    m[0][0] = (2.0f * tmp) * scaleS;
    m[0][1] = 0.0f;
    m[0][2] = 0.0f;
    m[0][3] = ((-(r + l) * tmp) * scaleS) + transS;

    tmp = 1.0f / (t - b);
    m[1][0] = 0.0f;
    m[1][1] = (2.0f * tmp) * scaleT;
    m[1][2] = 0.0f;
    m[1][3] = ((-(t + b) * tmp) * scaleT) + transT;

    m[2][0] = 0.0f;
    m[2][1] = 0.0f;
    m[2][2] = 0.0f;
    m[2][3] = 1.0f;
}
