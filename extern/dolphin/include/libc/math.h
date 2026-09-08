#ifndef _MATH_H_
#define _MATH_H_

#define NAN       (0.0f / 0.0f)
#define HUGE_VALF (1.0f / 0.0f)
#define INFINITY  (1.0f / 0.0f)

double fabs(double x);
double sin(double x);
double cos(double x);

float sinf(float x);
float cosf(float x);
float tanf(float x);
float acosf(float x);
float powf(float base, float exponent);

double ldexp(double x, int exp);

double scalbn(double x, int n);

double copysign(double x, double y);

#ifdef __MWERKS__
#pragma cplusplus on
#endif

double floor(double x);

extern inline float sqrtf(float x)
{
    static const double _half = .5;
    static const double _three = 3.0;
    volatile float y;
    if (x > 0.0f)
    {
#ifdef __MWERKS__
        double guess = __frsqrte((double)x);   // returns an approximation to
#elif defined(__PPC__) || defined(__powerpc__) || defined(__POWERPC__)
        double guess;
        asm("frsqrte %0, %1" : "=f"(guess) : "f"(x));
#else
        /* Host builds: frsqrte is a PowerPC instruction with no equivalent
           elsewhere, and the "f" constraint names a PPC float register. Use
           the host's sqrt so non-PowerPC targets can compile this header.
           NOTE: this is not bit-identical to the Newton-Raphson refinement
           below, so host builds may differ in the low bits from hardware. */
        double guess = 1.0 / __builtin_sqrt((double)x);
#endif
        guess = _half*guess*(_three - guess*guess*x);  // now have 12 sig bits
        guess = _half*guess*(_three - guess*guess*x);  // now have 24 sig bits
        guess = _half*guess*(_three - guess*guess*x);  // now have 32 sig bits
        y = (float)(x*guess);
        return y ;
    }
    return x;
}

// TODO: this isn't correct! It's just to generate sdata2 in GXDraw.o
extern inline float sqrt(float x)
{
    static const double _half = .5;
    static const double _three = 3.0;
    volatile float y;
    if (x > 0.0f)
    {
#ifdef __MWERKS__
        double guess = __frsqrte((double)x);   // returns an approximation to
#elif defined(__PPC__) || defined(__powerpc__) || defined(__POWERPC__)
        double guess;
        asm("frsqrte %0, %1" : "=f"(guess) : "f"(x));
#else
        /* Host builds: frsqrte is a PowerPC instruction with no equivalent
           elsewhere, and the "f" constraint names a PPC float register. Use
           the host's sqrt so non-PowerPC targets can compile this header.
           NOTE: this is not bit-identical to the Newton-Raphson refinement
           below, so host builds may differ in the low bits from hardware. */
        double guess = 1.0 / __builtin_sqrt((double)x);
#endif
        guess = _half*guess*(_three - guess*guess*x);  // now have 12 sig bits
        guess = _half*guess*(_three - guess*guess*x);  // now have 24 sig bits
        guess = _half*guess*(_three - guess*guess*x);  // now have 32 sig bits
        y = (float)(x*guess);
        return y ;
    }
    return x;
}

#ifdef __MWERKS__
#define fabs(x) __fabs(x)
#define fabsf(x) __fabsf(x)
#else
double fabs(double x);
float fabsf(float x);
#endif

long __fpclassifyf(float x);
long __fpclassifyd(double x);

#define FP_NAN       1
#define FP_INFINITE  2
#define FP_ZERO      3
#define FP_NORMAL    4
#define FP_SUBNORMAL 5

#define fpclassify(x) (sizeof(x) == sizeof(float) ? __fpclassifyf((float)(x)) : __fpclassifyd((double)(x)))
#define isfinite(x) ((fpclassify(x) > FP_INFINITE))

/* MWCC emits a single out-of-line copy for a bare `inline`; GCC/Clang under
   GNU89 inline rules emit one per translation unit, which then collide at
   link time. Keep the MWCC path byte-identical and mark it static elsewhere. */
#ifdef __MWERKS__
inline float fmodf(float x, float m)
#else
static inline float fmodf(float x, float m)
#endif
{
    float a = fabsf(m);
    float b = fabsf(x);
    if (a > b)
        return x;
    else
    {
        long long c = (long long)(x / m);
        return x - m * c;
    }
}

#ifdef __MWERKS__
#pragma cplusplus reset
#endif

#endif
