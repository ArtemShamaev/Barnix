/* Gouse Web: the cookie jar (RFC 6265: Domain, Path, Expires, Max-Age,
 * Secure) and saved passwords. Kept in memory given by the browser, saved
 * as text: cookies with an expiry date and every password survive restarts.
 * Passwords are only scrambled in their file, not encrypted: the file is in
 * the user's home folder. */
#include "web.h"
#include "web_jar.h"

typedef struct {
    char name[96], value[2048], domain[128], path[128];
    unsigned int expires;          /* seconds since 1970, 0 = this session */
    unsigned char secure, host_only, used;
} Cookie;
typedef struct { char host[128], user_field[64], user[128], password_field[64], password[128]; unsigned char used; } Saved;
#define MAX_COOKIES 300
#define MAX_SAVED 64
static Cookie *cookies;
static Saved *saved;
static int dirty_cookies, dirty_passwords;

int jar_init(void *memory, unsigned int bytes)
{
    unsigned int need = sizeof(Cookie) * MAX_COOKIES + sizeof(Saved) * MAX_SAVED;
    if (!memory || bytes < need) return -1;
    unsigned char *p = memory;
    for (unsigned int i = 0; i < need; i++) p[i] = 0;
    cookies = (Cookie *)p;
    saved = (Saved *)(p + sizeof(Cookie) * MAX_COOKIES);
    return 0;
}
unsigned int jar_bytes(void) { return sizeof(Cookie) * MAX_COOKIES + sizeof(Saved) * MAX_SAVED; }

/* ---------------------------------------------------------------- time */
unsigned int jar_epoch(int year, int month, int day, int hour, int minute, int second)
{
    /* days from 1970-01-01 (civil from days, Howard Hinnant) */
    int y = year - (month <= 2);
    int era = (y >= 0 ? y : y - 399) / 400;
    int yoe = y - era * 400;
    int doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int days = era * 146097 + doe - 719468;
    if (days < 0) return 0;
    return (unsigned int)days * 86400u + (unsigned int)(hour * 3600 + minute * 60 + second);
}
/* "Wed, 21 Oct 2026 07:28:00 GMT", "21-Oct-26 07:28:00 GMT" and friends. */
static unsigned int http_date(const char *s)
{
    static const char months[] = "janfebmaraprmayjunjulaugsepoctnovdec";
    int day = -1, month = -1, year = -1, h = -1, mi = 0, se = 0;
    while (*s) {
        while (*s && !((*s >= '0' && *s <= '9') || (*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z'))) s++;
        const char *t = s;
        while (*s && ((*s >= '0' && *s <= '9') || (*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || *s == ':')) s++;
        int n = (int)(s - t);
        if (!n) break;
        if (t[0] >= '0' && t[0] <= '9') {
            int colon = 0;
            for (int i = 0; i < n; i++) if (t[i] == ':') colon = 1;
            if (colon && h < 0) {
                h = 0; mi = 0; se = 0; int part = 0, v = 0;
                for (int i = 0; i <= n; i++) {
                    if (i == n || t[i] == ':') { if (part == 0) h = v; else if (part == 1) mi = v; else se = v; part++; v = 0; }
                    else v = v * 10 + t[i] - '0';
                }
            } else {
                int v = 0;
                for (int i = 0; i < n; i++) v = v * 10 + t[i] - '0';
                if (day < 0 && n <= 2 && month < 0) day = v;
                else if (day < 0 && n <= 2) day = v;
                else if (year < 0) year = n <= 2 ? (v < 70 ? 2000 + v : 1900 + v) : v;
            }
        } else if (n >= 3 && month < 0) {
            for (int m = 0; m < 12; m++)
                if (w_lower(t[0]) == months[m * 3] && w_lower(t[1]) == months[m * 3 + 1] && w_lower(t[2]) == months[m * 3 + 2]) month = m + 1;
        }
    }
    if (day < 1 || month < 1 || year < 1970 || h < 0) return 0;
    if (year > 2105) year = 2105;
    return jar_epoch(year, month, day, h, mi, se);
}

/* ---------------------------------------------------------------- URLs */
typedef struct { int secure; char host[128]; char path[512]; } Where;
static int where(const char *url, Where *w)
{
    w->secure = w_starts_ci(url, "https://");
    if (!w->secure && !w_starts_ci(url, "http://")) return -1;
    const char *p = url + (w->secure ? 8 : 7);
    int n = 0;
    while (*p && *p != '/' && *p != ':' && *p != '?' && *p != '#' && n < 127) w->host[n++] = (char)w_lower(*p++);
    w->host[n] = 0;
    while (*p && *p != '/' && *p != '?' && *p != '#') p++;
    n = 0;
    if (*p != '/') w->path[n++] = '/';
    while (*p && *p != '?' && *p != '#' && n < 511) w->path[n++] = *p++;
    w->path[n] = 0;
    return w->host[0] ? 0 : -1;
}
static int domain_match(const char *host, const char *domain)
{
    int hl = w_len(host), dl = w_len(domain);
    if (hl == dl) return w_eq_ci(host, domain);
    return hl > dl && host[hl - dl - 1] == '.' && w_eq_ci(host + hl - dl, domain);
}
static int path_match(const char *path, const char *cookie_path)
{
    int cl = w_len(cookie_path);
    for (int i = 0; i < cl; i++) if (path[i] != cookie_path[i]) return 0;
    return path[cl] == 0 || cookie_path[cl - 1] == '/' || path[cl] == '/';
}
static void trim_copy(char *out, int capacity, const char *s, int length)
{
    while (length && w_space(*s)) { s++; length--; }
    while (length && w_space(s[length - 1])) length--;
    if (length >= 2 && s[0] == '"' && s[length - 1] == '"') { s++; length -= 2; }
    if (length >= capacity) length = capacity - 1;
    for (int i = 0; i < length; i++) out[i] = s[i];
    out[length] = 0;
}

/* ---------------------------------------------------------------- cookies */
void jar_set_cookie(const char *url, const char *header, unsigned int now)
{
    Where w;
    if (!cookies || where(url, &w)) return;
    Cookie c;
    for (unsigned int i = 0; i < sizeof(c); i++) ((char *)&c)[i] = 0;
    const char *p = header, *eq = 0;
    while (*p && *p != ';') { if (*p == '=' && !eq) eq = p; p++; }
    if (!eq || eq == header) return;
    trim_copy(c.name, sizeof(c.name), header, (int)(eq - header));
    if ((int)(p - eq - 1) >= (int)sizeof(c.value)) return;   /* too long to keep */
    trim_copy(c.value, sizeof(c.value), eq + 1, (int)(p - eq - 1));
    if (!c.name[0]) return;
    int max_age_set = 0, delete = 0;
    long long max_age = 0;
    c.host_only = 1;
    w_copy(c.domain, w.host, sizeof(c.domain));
    {   /* default path: the request's directory */
        int n = w_len(w.path);
        while (n > 1 && w.path[n - 1] != '/') n--;
        if (n > 1) n--;
        trim_copy(c.path, sizeof(c.path), w.path, n);
        if (!c.path[0]) w_copy(c.path, "/", sizeof(c.path));
    }
    while (*p == ';') {
        p++;
        const char *a = p, *aeq = 0;
        while (*p && *p != ';') { if (*p == '=' && !aeq) aeq = p; p++; }
        char name[32], value[256];
        trim_copy(name, sizeof(name), a, (int)((aeq ? aeq : p) - a));
        if (aeq) trim_copy(value, sizeof(value), aeq + 1, (int)(p - aeq - 1)); else value[0] = 0;
        if (w_eq_ci(name, "expires")) { unsigned int t = http_date(value); if (t) c.expires = t; else if (value[0]) {} if (t && t <= now) delete = 1; }
        else if (w_eq_ci(name, "max-age")) {
            max_age_set = 1; max_age = 0; int negative = value[0] == '-';
            for (const char *q = value + negative; *q >= '0' && *q <= '9'; q++) if (max_age < 400000000) max_age = max_age * 10 + *q - '0';
            if (negative || max_age == 0) delete = 1;
        } else if (w_eq_ci(name, "domain") && value[0]) {
            const char *d = value[0] == '.' ? value + 1 : value;
            char lower[128]; int k = 0;
            while (d[k] && k < 127) { lower[k] = (char)w_lower(d[k]); k++; }
            lower[k] = 0;
            if (!domain_match(w.host, lower)) return;   /* not for this site */
            int dots = 0;
            for (int i = 0; lower[i]; i++) if (lower[i] == '.') dots++;
            if (!dots && !w_eq(lower, w.host)) return;   /* a top-level domain */
            w_copy(c.domain, lower, sizeof(c.domain));
            c.host_only = 0;
        } else if (w_eq_ci(name, "path") && value[0] == '/') w_copy(c.path, value, sizeof(c.path));
        else if (w_eq_ci(name, "secure")) c.secure = 1;
    }
    if (max_age_set && !delete) c.expires = now + (unsigned int)max_age;
    if (c.name[0] == '_' && c.name[1] == '_' && w_starts_ci(c.name, "__Secure-") && !w.secure) return;
    /* replace the same cookie, or take a free (or the oldest) place */
    int slot = -1, free_slot = -1, oldest = -1;
    for (int i = 0; i < MAX_COOKIES; i++) {
        Cookie *k = &cookies[i];
        if (!k->used) { if (free_slot < 0) free_slot = i; continue; }
        if (k->expires && k->expires <= now) { k->used = 0; if (free_slot < 0) free_slot = i; continue; }
        if (w_eq(k->name, c.name) && w_eq_ci(k->domain, c.domain) && w_eq(k->path, c.path)) slot = i;
        if (oldest < 0 || (k->expires && k->expires < cookies[oldest].expires)) oldest = i;
    }
    if (delete) { if (slot >= 0) { cookies[slot].used = 0; dirty_cookies = 1; } return; }
    if (slot < 0) slot = free_slot >= 0 ? free_slot : oldest;
    if (slot < 0) return;
    c.used = 1;
    cookies[slot] = c;
    if (c.expires) dirty_cookies = 1;
}
int jar_cookie_header(const char *url, char *out, int capacity, unsigned int now)
{
    Where w;
    out[0] = 0;
    if (!cookies || where(url, &w)) return 0;
    int n = 0, count = 0;
    /* longer paths first (RFC 6265 5.4) */
    for (int length = 128; length >= 0; length--)
        for (int i = 0; i < MAX_COOKIES; i++) {
            Cookie *k = &cookies[i];
            if (!k->used || w_len(k->path) != length) continue;
            if (k->expires && k->expires <= now) { k->used = 0; continue; }
            if (k->secure && !w.secure) continue;
            if (k->host_only ? !w_eq_ci(w.host, k->domain) : !domain_match(w.host, k->domain)) continue;
            if (!path_match(w.path, k->path)) continue;
            int need = w_len(k->name) + w_len(k->value) + 3;
            if (n + need + 16 >= capacity) break;
            if (!count) { w_copy(out, "Cookie: ", capacity); n = 8; }
            else { out[n++] = ';'; out[n++] = ' '; }
            w_copy(out + n, k->name, capacity - n); n += w_len(k->name);
            out[n++] = '=';
            w_copy(out + n, k->value, capacity - n); n += w_len(k->value);
            count++;
        }
    if (count) { out[n++] = '\r'; out[n++] = '\n'; out[n] = 0; }
    return count;
}
int jar_cookie_count(void) { int n = 0; for (int i = 0; cookies && i < MAX_COOKIES; i++) n += cookies[i].used; return n; }
void jar_clear_cookies(void) { for (int i = 0; cookies && i < MAX_COOKIES; i++) cookies[i].used = 0; dirty_cookies = 1; }
int jar_cookies_dirty(void) { return dirty_cookies; }

/* One line per cookie: domain TAB host-only TAB path TAB secure TAB expires TAB name TAB value. */
static const char *field(const char *s, char *out, int capacity)
{
    int n = 0;
    while (*s && *s != '\t' && *s != '\n') { if (n + 1 < capacity) out[n++] = *s; s++; }
    out[n] = 0;
    return *s == '\t' ? s + 1 : s;
}
static unsigned int number(const char *s) { unsigned int v = 0; while (*s >= '0' && *s <= '9') v = v * 10 + (unsigned)(*s++ - '0'); return v; }
void jar_load_cookies(const char *text, unsigned int now)
{
    if (!cookies || !text) return;
    int k = 0;
    for (const char *line = text; *line && k < MAX_COOKIES;) {
        Cookie c;
        char flag[8], expires[16], secure[8];
        for (unsigned int i = 0; i < sizeof(c); i++) ((char *)&c)[i] = 0;
        const char *p = field(line, c.domain, sizeof(c.domain));
        p = field(p, flag, sizeof(flag));
        p = field(p, c.path, sizeof(c.path));
        p = field(p, secure, sizeof(secure));
        p = field(p, expires, sizeof(expires));
        p = field(p, c.name, sizeof(c.name));
        p = field(p, c.value, sizeof(c.value));
        c.host_only = flag[0] == '1'; c.secure = secure[0] == '1'; c.expires = number(expires);
        if (c.name[0] && c.domain[0] && c.expires > now) { c.used = 1; cookies[k++] = c; }
        while (*p && *p != '\n') p++;
        line = *p ? p + 1 : p;
    }
    dirty_cookies = 0;
}
int jar_save_cookies(char *out, int capacity, unsigned int now)
{
    int n = 0;
    for (int i = 0; cookies && i < MAX_COOKIES; i++) {
        Cookie *k = &cookies[i];
        if (!k->used || !k->expires || k->expires <= now) continue;
        char e[12];
        w_itoa(e, (int)(k->expires > 0x7fffffffu ? 0x7fffffffu : k->expires));
        int need = w_len(k->domain) + w_len(k->path) + w_len(k->name) + w_len(k->value) + 32;
        if (n + need >= capacity) break;
        const char *parts[7] = {k->domain, k->host_only ? "1" : "0", k->path, k->secure ? "1" : "0", e, k->name, k->value};
        for (int f = 0; f < 7; f++) { w_copy(out + n, parts[f], capacity - n); n += w_len(parts[f]); out[n++] = f < 6 ? '\t' : '\n'; }
    }
    out[n] = 0;
    dirty_cookies = 0;
    return n;
}

/* ---------------------------------------------------------------- passwords */
static const char scramble_key[] = "Gouse Web keeps your passwords in your home folder";
static void scramble(const char *in, char *out, int capacity)   /* to hex */
{
    static const char hex[] = "0123456789abcdef";
    int n = 0, k = w_len(scramble_key);
    for (int i = 0; in[i] && n + 3 < capacity; i++) {
        unsigned char v = (unsigned char)(in[i] ^ scramble_key[i % k] ^ (i * 29));
        out[n++] = hex[v >> 4]; out[n++] = hex[v & 15];
    }
    out[n] = 0;
}
static void unscramble(const char *in, char *out, int capacity)
{
    int n = 0, k = w_len(scramble_key);
    for (int i = 0; in[2 * i] && in[2 * i + 1] && n + 1 < capacity; i++) {
        int a = in[2 * i], b = in[2 * i + 1];
        int v = (a <= '9' ? a - '0' : a - 'a' + 10) << 4 | (b <= '9' ? b - '0' : b - 'a' + 10);
        out[n++] = (char)(v ^ scramble_key[i % k] ^ (i * 29));
    }
    out[n] = 0;
}
static void host_of(const char *url, char *out, int capacity)
{
    Where w;
    if (where(url, &w)) { w_copy(out, url, capacity); return; }
    w_copy(out, w.host, capacity);
}
/* The saved login of url's site: 0 when there is one. */
int jar_password(const char *url, char *user_field, char *user, char *password_field, char *password, int capacity)
{
    char host[128];
    host_of(url, host, sizeof(host));
    for (int i = 0; saved && i < MAX_SAVED; i++)
        if (saved[i].used && w_eq_ci(saved[i].host, host)) {
            w_copy(user_field, saved[i].user_field, capacity); w_copy(user, saved[i].user, capacity);
            w_copy(password_field, saved[i].password_field, capacity); w_copy(password, saved[i].password, capacity);
            return 0;
        }
    return -1;
}
int jar_password_known(const char *url, const char *user, const char *password)
{
    char uf[64], u[128], pf[64], p[128];
    if (jar_password(url, uf, u, pf, p, 64)) return 0;
    return w_eq(u, user) && w_eq(p, password);
}
void jar_save_password(const char *url, const char *user_field, const char *user, const char *password_field, const char *password)
{
    char host[128];
    host_of(url, host, sizeof(host));
    int slot = -1;
    for (int i = 0; saved && i < MAX_SAVED; i++) if (saved[i].used && w_eq_ci(saved[i].host, host)) slot = i;
    for (int i = 0; saved && i < MAX_SAVED && slot < 0; i++) if (!saved[i].used) slot = i;
    if (slot < 0) slot = 0;
    Saved *s = &saved[slot];
    w_copy(s->host, host, sizeof(s->host));
    w_copy(s->user_field, user_field ? user_field : "", sizeof(s->user_field)); w_copy(s->user, user ? user : "", sizeof(s->user));
    w_copy(s->password_field, password_field, sizeof(s->password_field)); w_copy(s->password, password, sizeof(s->password));
    s->used = 1;
    dirty_passwords = 1;
}
int jar_password_count(void) { int n = 0; for (int i = 0; saved && i < MAX_SAVED; i++) n += saved[i].used; return n; }
const char *jar_password_site(int index, const char **user)
{
    for (int i = 0; saved && i < MAX_SAVED; i++) if (saved[i].used && index-- == 0) { *user = saved[i].user; return saved[i].host; }
    return 0;
}
void jar_clear_passwords(void) { for (int i = 0; saved && i < MAX_SAVED; i++) saved[i].used = 0; dirty_passwords = 1; }
int jar_passwords_dirty(void) { return dirty_passwords; }
/* host TAB user-field TAB user TAB password-field TAB scrambled password */
void jar_load_passwords(const char *text)
{
    if (!saved || !text) return;
    int k = 0;
    for (const char *line = text; *line && k < MAX_SAVED;) {
        Saved s;
        char scrambled[300];
        for (unsigned int i = 0; i < sizeof(s); i++) ((char *)&s)[i] = 0;
        const char *p = field(line, s.host, sizeof(s.host));
        p = field(p, s.user_field, sizeof(s.user_field));
        p = field(p, s.user, sizeof(s.user));
        p = field(p, s.password_field, sizeof(s.password_field));
        p = field(p, scrambled, sizeof(scrambled));
        unscramble(scrambled, s.password, sizeof(s.password));
        if (s.host[0] && s.password_field[0]) { s.used = 1; saved[k++] = s; }
        while (*p && *p != '\n') p++;
        line = *p ? p + 1 : p;
    }
    dirty_passwords = 0;
}
int jar_save_passwords(char *out, int capacity)
{
    int n = 0;
    for (int i = 0; saved && i < MAX_SAVED; i++) {
        Saved *s = &saved[i];
        if (!s->used) continue;
        char scrambled[300];
        scramble(s->password, scrambled, sizeof(scrambled));
        const char *parts[5] = {s->host, s->user_field, s->user, s->password_field, scrambled};
        int need = 0;
        for (int f = 0; f < 5; f++) need += w_len(parts[f]) + 1;
        if (n + need + 1 >= capacity) break;
        for (int f = 0; f < 5; f++) { w_copy(out + n, parts[f], capacity - n); n += w_len(parts[f]); out[n++] = f < 4 ? '\t' : '\n'; }
    }
    out[n] = 0;
    dirty_passwords = 0;
    return n;
}
