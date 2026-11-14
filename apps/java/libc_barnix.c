/* The C library pieces BJVM needs on Barnix: string functions, strtod and a
 * vsnprintf with widths, precisions and %e. Decimal conversions of doubles
 * are exact (big integers), so Double.toString round-trips. */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
int strncmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) {
        if (*a != *b) return (unsigned char)*a - (unsigned char)*b;
        if (!*a) return 0;
    }
    return 0;
}
char *strchr(const char *s, int c)
{
    for (;; s++) {
        if (*s == (char)c) return (char *)s;
        if (!*s) return 0;
    }
}
char *strrchr(const char *s, int c)
{
    const char *last = 0;
    for (;; s++) {
        if (*s == (char)c) last = s;
        if (!*s) return (char *)last;
    }
}
long strtol(const char *s, char **end, int base)
{
    const char *p = s;
    while (*p == ' ' || *p == '\t' || *p == '\n') p++;
    int negative = 0;
    if (*p == '-' || *p == '+') negative = *p++ == '-';
    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) { p += 2; base = 16; }
    else if (base == 0) base = *p == '0' ? 8 : 10;
    unsigned long v = 0;
    const char *digits = p;
    for (;; p++) {
        int d = *p >= '0' && *p <= '9' ? *p - '0' : *p >= 'a' && *p <= 'z' ? *p - 'a' + 10 : *p >= 'A' && *p <= 'Z' ? *p - 'A' + 10 : 99;
        if (d >= base) break;
        v = v * (unsigned long)base + (unsigned long)d;
    }
    if (end) *end = (char *)(p == digits ? s : p);
    return negative ? -(long)v : (long)v;
}
/* glibc's headers name strtol this way in C23 mode. */
long __isoc23_strtol(const char *s, char **end, int base) { return strtol(s, end, base); }
int atoi(const char *s)
{
    int sign = 1, v = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-' || *s == '+') { if (*s == '-') sign = -1; s++; }
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return sign * v;
}

/* Exact decimal conversion with small big integers (little-endian 32-bit
 * words): enough for any double times a power of ten up to 10^400. */
#define BIG_WORDS 96
typedef struct { uint32_t w[BIG_WORDS]; int n; } Big;
static void big_set(Big *b, uint64_t v) { b->n = 0; while (v) { b->w[b->n++] = (uint32_t)v; v >>= 32; } }
static void big_mul_small(Big *b, uint32_t m)
{
    uint64_t carry = 0;
    for (int i = 0; i < b->n; i++) { uint64_t t = (uint64_t)b->w[i] * m + carry; b->w[i] = (uint32_t)t; carry = t >> 32; }
    if (carry && b->n < BIG_WORDS) b->w[b->n++] = (uint32_t)carry;
}
static void big_pow10(Big *b, int e) { for (; e >= 9; e -= 9) big_mul_small(b, 1000000000U); while (e-- > 0) big_mul_small(b, 10); }
static void big_shl(Big *b, int bits)
{
    if (!b->n || bits <= 0) return;
    int words = bits / 32, r = bits % 32;
    if (b->n + words + 1 > BIG_WORDS) return;
    b->w[b->n + words] = 0;
    for (int i = b->n - 1; i >= 0; i--) {
        b->w[i + words + 1] |= r ? b->w[i] >> (32 - r) : 0;
        b->w[i + words] = b->w[i] << r;
    }
    for (int i = 0; i < words; i++) b->w[i] = 0;
    b->n += words + 1;
    while (b->n && !b->w[b->n - 1]) b->n--;
}
static int big_cmp(const Big *a, const Big *b)
{
    if (a->n != b->n) return a->n < b->n ? -1 : 1;
    for (int i = a->n - 1; i >= 0; i--) if (a->w[i] != b->w[i]) return a->w[i] < b->w[i] ? -1 : 1;
    return 0;
}
static void big_sub(Big *a, const Big *b)   /* a >= b */
{
    int64_t borrow = 0;
    for (int i = 0; i < a->n; i++) {
        int64_t t = (int64_t)a->w[i] - (i < b->n ? b->w[i] : 0) - borrow;
        borrow = t < 0; a->w[i] = (uint32_t)(t + (borrow ? 0x100000000LL : 0));
    }
    while (a->n && !a->w[a->n - 1]) a->n--;
}
static int big_bits(const Big *b) { if (!b->n) return 0; int n = 32; while (!(b->w[b->n - 1] >> (n - 1))) n--; return (b->n - 1) * 32 + n; }
/* num / den for a quotient below 2^63; num keeps the remainder. */
static uint64_t big_div(Big *num, const Big *den)
{
    uint64_t q = 0;
    int shift = big_bits(num) - big_bits(den);
    if (shift > 62) shift = 62;
    for (; shift >= 0; shift--) {
        Big d = *den;
        big_shl(&d, shift);
        if (big_cmp(num, &d) >= 0) { big_sub(num, &d); q |= 1ULL << shift; }
    }
    return q;
}
static void split(double v, uint64_t *mantissa, int *exponent)   /* v = mantissa * 2^exponent, v > 0 */
{
    union { double d; uint64_t u; } x = {v};
    int e = (int)(x.u >> 52 & 2047);
    uint64_t m = x.u & ((1ULL << 52) - 1);
    if (e) m |= 1ULL << 52; else e = 1;
    *mantissa = m; *exponent = e - 1075;
}
/* round-half-even(v * 10^shift) for positive v; *whole gets the floor. */
static uint64_t scaled_digits(double v, int shift, uint64_t *whole)
{
    uint64_t m; int k;
    split(v, &m, &k);
    Big num, den;
    big_set(&num, m); big_set(&den, 1);
    if (k > 0) big_shl(&num, k); else big_shl(&den, -k);
    if (shift > 0) big_pow10(&num, shift); else big_pow10(&den, -shift);
    uint64_t q = big_div(&num, &den);
    *whole = q;
    big_shl(&num, 1);
    int c = big_cmp(&num, &den);
    if (c > 0 || (c == 0 && (q & 1))) q++;
    return q;
}

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static int match(const char *s, const char *word)
{
    int i = 0;
    for (; word[i]; i++) if (lower((unsigned char)s[i]) != word[i]) return 0;
    return i;
}

/* Correctly rounded digits * 10^exponent (digits has at most 19 digits). */
static double decimal_to_double(uint64_t digits, int exponent)
{
    if (!digits) return 0;
    if (exponent > 310) return __builtin_inf();
    if (exponent < -345) return 0;
    Big num, den;
    big_set(&num, digits); big_set(&den, 1);
    if (exponent > 0) big_pow10(&num, exponent); else big_pow10(&den, -exponent);
    /* Scale so the quotient has 54 or 55 bits; the 2^-k goes to the exponent. */
    int k = 55 - (big_bits(&num) - big_bits(&den));
    if (k > 0) big_shl(&num, k); else big_shl(&den, -k);
    uint64_t q = big_div(&num, &den);
    int sticky = num.n != 0;
    int binary = -k;
    while (q >= 1ULL << 54) { sticky |= (int)(q & 1); q >>= 1; binary++; }
    /* Keep 53 bits; denormals keep fewer, down to the 2^-1074 place. */
    int drop = 1;
    if (binary + drop < -1074) drop = -1074 - binary;
    if (drop > 60) return 0;
    uint64_t half = 1ULL << (drop - 1), rest = q & ((1ULL << drop) - 1);
    uint64_t m = q >> drop;
    if (rest > half || (rest == half && (sticky || (m & 1)))) m++;
    binary += drop;
    double r = (double)m;
    /* m * 2^binary without libm: scale in steps that stay exact. */
    while (binary > 0) { int s = binary > 60 ? 60 : binary; r *= (double)(1ULL << s); binary -= s; }
    while (binary < 0) { int s = -binary > 60 ? 60 : -binary; r /= (double)(1ULL << s); binary += s; }
    return r;
}

double strtod(const char *s, char **end)
{
    const char *p = s;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    int negative = 0;
    if (*p == '-' || *p == '+') negative = *p++ == '-';
    int n;
    if ((n = match(p, "infinity")) || (n = match(p, "inf"))) {
        if (end) *end = (char *)(p + n);
        return negative ? -__builtin_inf() : __builtin_inf();
    }
    if ((n = match(p, "nan"))) { if (end) *end = (char *)(p + n); return __builtin_nan(""); }
    uint64_t digits = 0;
    int count = 0, exponent = 0, any = 0;
    for (; *p >= '0' && *p <= '9'; p++, any = 1) {
        if (count < 19) { if (digits || *p != '0') { digits = digits * 10 + (uint64_t)(*p - '0'); count++; } }
        else exponent++;
    }
    if (*p == '.') {
        p++;
        for (; *p >= '0' && *p <= '9'; p++, any = 1)
            if (count < 19) { if (digits || *p != '0') { digits = digits * 10 + (uint64_t)(*p - '0'); count++; } exponent--; }
    }
    if (!any) { if (end) *end = (char *)s; return 0; }
    if (*p == 'e' || *p == 'E') {
        const char *q = p + 1;
        int sign = 1, e = 0, edigits = 0;
        if (*q == '-' || *q == '+') { if (*q == '-') sign = -1; q++; }
        for (; *q >= '0' && *q <= '9'; q++, edigits++) if (e < 100000) e = e * 10 + (*q - '0');
        if (edigits) { exponent += sign * e; p = q; }
    }
    if (end) *end = (char *)p;
    double v = decimal_to_double(digits, exponent);
    return negative ? -v : v;
}

/* ---- formatting ---- */
typedef struct { char *buffer; size_t capacity, length; } Out;
static void put(Out *o, char c)
{
    if (o->length + 1 < o->capacity) o->buffer[o->length] = c;
    o->length++;
}
static void put_padded(Out *o, const char *text, int length, int width, int left, char pad)
{
    if (pad == '0' && !left && length && (text[0] == '-' || text[0] == '+' || text[0] == ' ')) {
        put(o, *text++); length--; width--;
    }
    if (!left) for (int i = length; i < width; i++) put(o, pad);
    for (int i = 0; i < length; i++) put(o, text[i]);
    if (left) for (int i = length; i < width; i++) put(o, ' ');
}
static int unsigned_text(char *out, uint64_t v, unsigned int base, int upper, int precision)
{
    char r[24];
    int n = 0;
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    do { r[n++] = digits[v % base]; v /= base; } while (v);
    int k = 0;
    for (int i = n; i < precision; i++) out[k++] = '0';
    while (n) out[k++] = r[--n];
    return k;
}
static uint64_t pow10u(int n) { uint64_t r = 1; while (n-- > 0) r *= 10; return r; }
/* d.ddde[+-]xx with precision digits after the point, exactly rounded. */
static int exp_text(char *out, double v, int precision, int upper)
{
    int k = 0;
    if (precision > 17) precision = 17;
    if (v != v) { out[0] = 'n'; out[1] = 'a'; out[2] = 'n'; return 3; }
    if (__builtin_signbit(v)) { out[k++] = '-'; v = -v; }
    if (v == __builtin_inf()) { out[k++] = 'i'; out[k++] = 'n'; out[k++] = 'f'; return k; }
    int exponent = 0;
    char d[24];
    if (v != 0) {
        /* Estimate floor(log10 v) from the binary exponent, then correct it. */
        uint64_t m; int b;
        split(v, &m, &b);
        int bits = 0;
        while (m >> bits) bits++;
        double l = (b + bits - 1) * 0.30102999566398120;
        int e = (int)l - (l < (int)l);
        uint64_t n = 0, whole;
        /* The floor of v * 10^(precision - e) has precision + 1 digits. */
        for (int tries = 0; tries < 6; tries++) {
            n = scaled_digits(v, precision - e, &whole);
            if (whole >= pow10u(precision + 1)) e++;
            else if (whole < pow10u(precision)) e--;
            else break;
        }
        if (n >= pow10u(precision + 1)) { n /= 10; e++; }   /* 9.99... rounded up */
        exponent = e;
        for (int i = precision; i >= 0; i--) { d[i] = (char)('0' + n % 10); n /= 10; }
    } else for (int i = 0; i <= precision; i++) d[i] = '0';
    out[k++] = d[0];
    if (precision) { out[k++] = '.'; for (int i = 1; i <= precision; i++) out[k++] = d[i]; }
    out[k++] = upper ? 'E' : 'e';
    out[k++] = exponent < 0 ? '-' : '+';
    unsigned int a = exponent < 0 ? (unsigned int)-exponent : (unsigned int)exponent;
    k += unsigned_text(out + k, a, 10, 0, 2);
    return k;
}

int vsnprintf(char *buffer, size_t capacity, const char *format, va_list ap)
{
    Out o = {buffer, capacity, 0};
    for (; *format; format++) {
        if (*format != '%') { put(&o, *format); continue; }
        format++;
        int left = 0, plus = 0, space = 0, alt = 0;
        char pad = ' ';
        for (;; format++) {
            if (*format == '-') left = 1;
            else if (*format == '0') pad = '0';
            else if (*format == '+') plus = 1;
            else if (*format == ' ') space = 1;
            else if (*format == '#') alt = 1;
            else break;
        }
        int width = 0, precision = -1;
        if (*format == '*') { width = va_arg(ap, int); if (width < 0) { left = 1; width = -width; } format++; }
        else while (*format >= '0' && *format <= '9') width = width * 10 + (*format++ - '0');
        if (*format == '.') {
            format++;
            precision = 0;
            if (*format == '*') { precision = va_arg(ap, int); format++; }
            else while (*format >= '0' && *format <= '9') precision = precision * 10 + (*format++ - '0');
        }
        int longs = 0;
        for (;; format++) {
            if (*format == 'l') longs++;
            else if (*format == 'h' || *format == 'z' || *format == 't' || *format == 'j') { if (*format == 'j') longs = 2; }
            else break;
        }
        char text[64];
        int n = 0;
        switch (*format) {
        case 'd': case 'i': {
            int64_t v = longs >= 2 ? va_arg(ap, long long) : longs ? va_arg(ap, long) : va_arg(ap, int);
            uint64_t u = v < 0 ? (uint64_t)0 - (uint64_t)v : (uint64_t)v;
            if (v < 0) text[n++] = '-';
            else if (plus) text[n++] = '+';
            else if (space) text[n++] = ' ';
            n += unsigned_text(text + n, u, 10, 0, precision);
            put_padded(&o, text, n, width, left, precision >= 0 ? ' ' : pad);
            break;
        }
        case 'u': case 'x': case 'X': case 'o': {
            uint64_t v = longs >= 2 ? va_arg(ap, unsigned long long) : longs ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int);
            unsigned int base = *format == 'u' ? 10 : *format == 'o' ? 8 : 16;
            if (alt && base == 16 && v) { text[n++] = '0'; text[n++] = *format; }
            n += unsigned_text(text + n, v, base, *format == 'X', precision);
            put_padded(&o, text, n, width, left, precision >= 0 ? ' ' : pad);
            break;
        }
        case 'p': {
            uintptr_t v = (uintptr_t)va_arg(ap, void *);
            text[n++] = '0'; text[n++] = 'x';
            n += unsigned_text(text + n, v, 16, 0, -1);
            put_padded(&o, text, n, width, left, ' ');
            break;
        }
        case 'c': text[0] = (char)va_arg(ap, int); put_padded(&o, text, 1, width, left, ' '); break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            int length = 0;
            while (s[length] && (precision < 0 || length < precision)) length++;
            put_padded(&o, s, length, width, left, ' ');
            break;
        }
        case 'e': case 'E': {
            double v = va_arg(ap, double);
            n = exp_text(text, v, precision < 0 ? 6 : precision, *format == 'E');
            if (plus && text[0] != '-') { for (int i = n; i > 0; i--) text[i] = text[i - 1]; text[0] = '+'; n++; }
            put_padded(&o, text, n, width, left, pad);
            break;
        }
        case '%': put(&o, '%'); break;
        case 0: format--; break;
        default: put(&o, '%'); put(&o, *format); break;
        }
    }
    if (capacity) buffer[o.length < capacity ? o.length : capacity - 1] = 0;
    return (int)o.length;
}
int snprintf(char *buffer, size_t capacity, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    int n = vsnprintf(buffer, capacity, format, ap);
    va_end(ap);
    return n;
}
