/* pc_libc.c — the few C library functions the decomp does not supply itself.
 *
 * src/MSL is the Metrowerks standard library, and it covers almost everything
 * the game calls: memcpy, memset, the string functions, printf, strtoul, sinf,
 * cosf, tanf. Four routines are missing because on hardware they came from the
 * Metrowerks runtime rather than from MSL's sources.
 *
 * sqrt, sqrtf and floor were meant to compile to single x86 instructions via
 * GCC builtins, and do at -O2 -- but this build is -O0 (CFLAGS in
 * tools/phase0/linkexe.sh), and at -O0 GCC does not lower __builtin_sqrt(f)
 * to a hardware instruction; it lowers it to a *call* to the C library
 * function named sqrt(f), on the assumption libm provides one. Since this
 * file *is* that library function, the call resolved to itself: infinite
 * recursion, confirmed via gdb showing sqrtf calling sqrtf calling sqrtf
 * until the stack gave out. Inline asm sidesteps the lowering question
 * entirely -- there is no call for -O0 to get wrong. floor has no matching
 * x87 opcode (frndint follows the FPU's rounding-control word, not
 * round-toward-negative-infinity), so it stays in portable C instead of
 * builtin form, for the same self-recursion reason.
 */
#include "pc_libc.h"

#include <stddef.h> /* NULL */

double sqrt(double x)
{
    double r;
    __asm__("fsqrt" : "=t"(r) : "0"(x));
    return r;
}

float sqrtf(float x)
{
    float r;
    __asm__("fsqrt" : "=t"(r) : "0"(x));
    return r;
}

double floor(double x)
{
    double truncated = (double) (long long) x;
    if (x < 0.0 && truncated != x) {
        truncated -= 1.0;
    }
    return truncated;
}

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

/* --- 64-bit division helpers ---
 *
 * On a 32-bit target the compiler lowers 64-bit division to calls into libgcc.
 * A -nostdlib link does not have it, and the host's libgcc.a is the wrong
 * architecture, so the three routines the code generator actually emits are
 * provided here. OSGetTime is the first caller: it scales a nanosecond count
 * into the console's tick rate.
 *
 * Plain restoring division, one bit at a time. Correctness matters more than
 * speed -- nothing on a hot path divides 64-bit values.
 */
static unsigned long long udivmod64(unsigned long long n, unsigned long long d,
                                    unsigned long long* rem)
{
    unsigned long long q = 0;
    unsigned long long r = 0;
    int i;

    if (d == 0) {
        if (rem != NULL) {
            *rem = 0;
        }
        return 0; /* undefined; do not fault */
    }

    for (i = 63; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1u);
        if (r >= d) {
            r -= d;
            q |= 1ull << i;
        }
    }
    if (rem != NULL) {
        *rem = r;
    }
    return q;
}

unsigned long long __udivdi3(unsigned long long a, unsigned long long b)
{
    return udivmod64(a, b, 0);
}

unsigned long long __umoddi3(unsigned long long a, unsigned long long b)
{
    unsigned long long r;
    udivmod64(a, b, &r);
    return r;
}

long long __divdi3(long long a, long long b)
{
    int negative = 0;
    unsigned long long ua, ub, q;

    if (a < 0) { a = -a; negative = !negative; }
    if (b < 0) { b = -b; negative = !negative; }
    ua = (unsigned long long) a;
    ub = (unsigned long long) b;
    q = udivmod64(ua, ub, 0);
    return negative ? -(long long) q : (long long) q;
}
