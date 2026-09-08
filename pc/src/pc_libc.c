/* pc_libc.c — the few C library functions the decomp does not supply itself.
 *
 * src/MSL is the Metrowerks standard library, and it covers almost everything
 * the game calls: memcpy, memset, the string functions, printf, strtoul, sinf,
 * cosf, tanf. Four routines are missing because on hardware they came from the
 * Metrowerks runtime rather than from MSL's sources.
 *
 * sqrt, sqrtf and floor compile to single x86 instructions via GCC builtins,
 * so they cost nothing and are exact.
 */
#include "pc_libc.h"

double sqrt(double x)  { return __builtin_sqrt(x); }
float  sqrtf(float x)  { return __builtin_sqrtf(x); }
double floor(double x) { return __builtin_floor(x); }

/* atanf has no builtin. This is the standard odd-polynomial approximation on
 * |x| <= 1 with the usual reduction for larger arguments -- accurate to a few
 * ULP, which is fine for reaching a boot screen.
 *
 * It is NOT bit-identical to the Metrowerks routine the console used. Melee is
 * deterministic and its physics run through these functions, so a real port
 * wants the original algorithm here before anything depends on frame-exact
 * behaviour. Callers today are ftcoll.c, lb_00CE.c and bytecode.c.
 */
float atanf(float x)
{
    static const float pi_2 = 3.14159265358979323846f / 2.0f;
    float ax, z, r;
    int inverted;

    if (x != x) {
        return x; /* NaN */
    }

    ax = x < 0.0f ? -x : x;
    inverted = ax > 1.0f;
    z = inverted ? 1.0f / ax : ax;

    /* Horner form of the minimax polynomial for atan on [0, 1]. */
    r = z * z;
    r = ((((0.0208351f * r - 0.0851330f) * r + 0.1801410f) * r - 0.3302995f) *
             r +
         0.9998660f) *
        z;

    if (inverted) {
        r = pi_2 - r;
    }
    return x < 0.0f ? -r : r;
}
