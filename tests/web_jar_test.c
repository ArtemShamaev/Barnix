/* Cookie jar and saved passwords of Gouse Web (apps/gouse/web_jar.c). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "apps/gouse/web.h"
#include "apps/gouse/web_jar.h"
int web_fetch(const char *u, WebResource *o, int m, const char *p, const char *t) { (void)u; (void)o; (void)m; (void)p; (void)t; return -1; }
long web_inflate(const void *i, unsigned long l, void *o, unsigned long c, int z) { (void)i; (void)l; (void)o; (void)c; (void)z; return -1; }
void web_progress(const char *t) { (void)t; }
static int failures;
#define CHECK(name, cond) do { if (!(cond)) { failures++; printf("FAIL %s\n", name); } } while (0)
int main(void)
{
    static unsigned char memory[2 << 20];
    char h[4096];
    unsigned int now = jar_epoch(2026, 10, 16, 12, 0, 0);
    CHECK("epoch", jar_epoch(1970, 1, 1, 0, 0, 0) == 0 && jar_epoch(2000, 3, 1, 0, 0, 0) == 951868800u);
    jar_init(memory, sizeof(memory));
    jar_set_cookie("https://www.example.com/a/b", "sid=abc123; Path=/; HttpOnly", now);
    jar_set_cookie("https://www.example.com/a/b", "pref=dark; Domain=.example.com; Path=/; Max-Age=3600", now);
    jar_set_cookie("https://www.example.com/a/b", "local=1", now);
    jar_set_cookie("http://www.example.com/", "safe=1; Secure", now);
    jar_set_cookie("https://www.example.com/", "evil=1; Domain=other.org", now);
    jar_set_cookie("https://www.example.com/", "old=1; Expires=Wed, 21 Oct 2015 07:28:00 GMT", now);
    jar_set_cookie("https://www.example.com/", "later=1; expires=Thu, 01-Jan-2032 00:00:00 GMT", now);
    jar_cookie_header("https://www.example.com/a/c", h, sizeof(h), now);
    CHECK("host cookie", strstr(h, "sid=abc123") != 0);
    CHECK("domain cookie", strstr(h, "pref=dark") != 0);
    CHECK("default path /a", strstr(h, "local=1") != 0);
    CHECK("secure over https", strstr(h, "safe=1") != 0);
    CHECK("foreign domain refused", !strstr(h, "evil"));
    CHECK("expired dropped", !strstr(h, "old="));
    CHECK("header form", !strncmp(h, "Cookie: ", 8) && h[strlen(h) - 1] == '\n');
    jar_cookie_header("http://www.example.com/", h, sizeof(h), now);
    CHECK("no secure over http", !strstr(h, "safe=1") && !strstr(h, "local=1") && strstr(h, "sid="));
    jar_cookie_header("https://shop.example.com/", h, sizeof(h), now);
    CHECK("subdomain gets domain cookie only", strstr(h, "pref=dark") && !strstr(h, "sid="));
    jar_cookie_header("https://www.example.com/", h, sizeof(h), now + 7200);
    CHECK("max-age runs out", !strstr(h, "pref="));
    jar_set_cookie("https://www.example.com/", "sid=gone; Max-Age=0", now);
    jar_cookie_header("https://www.example.com/", h, sizeof(h), now);
    CHECK("max-age=0 deletes", !strstr(h, "sid="));
    static char text[65536];
    int n = jar_save_cookies(text, sizeof(text), now);
    CHECK("only dated cookies saved", n > 0 && strstr(text, "later") && !strstr(text, "local") && !strstr(text, "safe"));
    jar_clear_cookies();
    jar_load_cookies(text, now);
    jar_cookie_header("https://www.example.com/", h, sizeof(h), now);
    CHECK("cookies come back", strstr(h, "later=1") && !strstr(h, "local"));
    jar_save_password("https://mail.example.com/login?x", "user", "artem", "pass", "секрет p@ss");
    n = jar_save_passwords(text, sizeof(text));
    CHECK("password file has no plain password", n > 0 && !strstr(text, "p@ss") && !strstr(text, "секрет"));
    jar_clear_passwords();
    jar_load_passwords(text);
    char uf[64], u[128], pf[64], p[128];
    CHECK("password found", !jar_password("https://mail.example.com/other", uf, u, pf, p, 64) && !strcmp(u, "artem") && !strcmp(p, "секрет p@ss") && !strcmp(pf, "pass"));
    CHECK("other site has none", jar_password("https://example.org/", uf, u, pf, p, 64));
    CHECK("known", jar_password_known("https://mail.example.com/", "artem", "секрет p@ss"));
    if (failures) { printf("%d cookie/password check(s) failed\n", failures); return 1; }
    puts("Gouse Web cookies (domain, path, secure, expiry, files) and saved passwords passed");
    return 0;
}
