#include <math.h>
/* <math.h> on x87 instructions. The FPU computes in extended precision, so
 * these are accurate to the last bit of double for ordinary arguments. */
double sqrt(double x) { double r; __asm__("fsqrt" : "=t"(r) : "0"(x)); return r; }
double sin(double x) { double r; __asm__("fsin" : "=t"(r) : "0"(x)); return r; }
double cos(double x) { double r; __asm__("fcos" : "=t"(r) : "0"(x)); return r; }
double tan(double x) { double r; __asm__("fptan; fstp %%st(0)" : "=t"(r) : "0"(x)); return r; }
double atan2(double y, double x) { double r; __asm__("fpatan" : "=t"(r) : "0"(x), "u"(y) : "st(1)"); return r; }
double atan(double x) { return atan2(x, 1.0); }
double asin(double x) { return atan2(x, sqrt((1.0 - x) * (1.0 + x))); }
double acos(double x) { return atan2(sqrt((1.0 - x) * (1.0 + x)), x); }
double fabs(double x) { double r; __asm__("fabs" : "=t"(r) : "0"(x)); return r; }
double rint(double x) { double r; __asm__("frndint" : "=t"(r) : "0"(x)); return r; }
double floor(double x) { double r = rint(x); return r > x ? r - 1.0 : r; }
double ceil(double x) { double r = rint(x); return r < x ? r + 1.0 : r; }
double trunc(double x) { return x < 0 ? ceil(x) : floor(x); }
double round(double x) { return x < 0 ? -floor(0.5 - x) : floor(x + 0.5); }
double log2(double x) { double r; __asm__("fld1; fxch; fyl2x" : "=t"(r) : "0"(x)); return r; }
double log(double x) { double r; __asm__("fldln2; fxch; fyl2x" : "=t"(r) : "0"(x)); return r; }
double log10(double x) { double r; __asm__("fldlg2; fxch; fyl2x" : "=t"(r) : "0"(x)); return r; }
double exp2(double x)
{
    if (isnan(x)) return x;
    if (x > 16384.0) return HUGE_VAL;
    if (x < -16384.0) return 0.0;
    double whole = rint(x), part = x - whole, r;
    __asm__("f2xm1" : "=t"(part) : "0"(part));
    part += 1.0;
    __asm__("fscale" : "=t"(r) : "0"(part), "u"(whole));
    return r;
}
double exp(double x) { return exp2(x * M_LOG2E); }
double pow(double x, double y)
{
    if (y == 0.0) return 1.0;
    if (x == 0.0) return y > 0 ? 0.0 : HUGE_VAL;
    if (x > 0.0) return exp2(y * log2(x));
    if (rint(y) != y) return NAN;
    double r = exp2(y * log2(-x));
    return fmod(y, 2.0) != 0.0 ? -r : r;
}
double fmod(double x, double y)
{
    double r;
    __asm__("1: fprem; fnstsw %%ax; testw $0x400, %%ax; jnz 1b" : "=t"(r) : "0"(x), "u"(y) : "ax", "cc");
    return r;
}
double hypot(double x, double y) { return sqrt(x * x + y * y); }
double fmin(double a, double b) { return a < b || isnan(b) ? a : b; }
double fmax(double a, double b) { return a > b || isnan(b) ? a : b; }
double sinh(double x) { double e = exp(x); return (e - 1.0 / e) * 0.5; }
double cosh(double x) { double e = exp(x); return (e + 1.0 / e) * 0.5; }
double tanh(double x)
{
    if (x > 20.0) return 1.0;
    if (x < -20.0) return -1.0;
    double e = exp(2.0 * x);
    return (e - 1.0) / (e + 1.0);
}

double cbrt(double x)
{
    if (x == 0 || isnan(x) || isinf(x)) return x;
    double r = x < 0 ? -pow(-x, 1.0 / 3.0) : pow(x, 1.0 / 3.0);
    return r - (r * r * r - x) / (3.0 * r * r);   /* one Newton step */
}
/* Accurate near 0, where exp(x) - 1 and log(1 + x) lose every digit. */
double expm1(double x)
{
    if (fabs(x) < 1e-5) return x + x * x * 0.5 + x * x * x / 6.0;
    return exp(x) - 1.0;
}
double log1p(double x)
{
    double u = 1.0 + x;
    if (u == 1.0) return x;
    return log(u) * x / (u - 1.0);
}
double remainder(double x, double y)
{
    if (isnan(x) || isnan(y) || isinf(x) || y == 0) return NAN;
    if (isinf(y)) return x;
    double n = rint(x / y);
    return x - n * y;
}
double copysign(double x, double y) { return signbit(x) == signbit(y) ? x : -x; }

float sqrtf(float x) { return (float)sqrt(x); }
float sinf(float x) { return (float)sin(x); }
float cosf(float x) { return (float)cos(x); }
float tanf(float x) { return (float)tan(x); }
float asinf(float x) { return (float)asin(x); }
float acosf(float x) { return (float)acos(x); }
float atanf(float x) { return (float)atan(x); }
float atan2f(float y, float x) { return (float)atan2(y, x); }
float expf(float x) { return (float)exp(x); }
float exp2f(float x) { return (float)exp2(x); }
float logf(float x) { return (float)log(x); }
float log2f(float x) { return (float)log2(x); }
float log10f(float x) { return (float)log10(x); }
float powf(float x, float y) { return (float)pow(x, y); }
float fabsf(float x) { return (float)fabs(x); }
float floorf(float x) { return (float)floor(x); }
float ceilf(float x) { return (float)ceil(x); }
float roundf(float x) { return (float)round(x); }
float truncf(float x) { return (float)trunc(x); }
float rintf(float x) { return (float)rint(x); }
float fmodf(float x, float y) { return (float)fmod(x, y); }
float hypotf(float x, float y) { return (float)hypot(x, y); }
float fminf(float a, float b) { return (float)fmin(a, b); }
float fmaxf(float a, float b) { return (float)fmax(a, b); }
float sinhf(float x) { return (float)sinh(x); }
float coshf(float x) { return (float)cosh(x); }
float tanhf(float x) { return (float)tanh(x); }
