/* Barnix Net HTTP/1.1 client: GET and POST over TCP or TLS, redirects,
 * Content-Length, chunked bodies and reading until the server closes;
 * keep-alive, so a page's styles, scripts and pictures share one
 * connection (and one TLS handshake). */
#include <barnix_net.h>
#include "bnet.h"
#include "tls.h"

static Tls tls;   /* one connection at a time: programs are single-threaded */
typedef struct {
    int handle, secure;
    uint8_t buffer[16384];
    unsigned int start, end;
} Stream;
static Stream stream;
/* The connection left open by a keep_alive request. */
static struct { int open, secure; unsigned int port; char host[256]; } kept;

static int tls_send(void *context, const void *data, unsigned int length)
{ return bnet_platform_send(*(int *)context, data, length); }
static int tls_recv(void *context, void *data, unsigned int capacity, unsigned int timeout_ms)
{ return bnet_platform_recv(*(int *)context, data, capacity, timeout_ms); }

static void set_error(BnetRequest *r, const char *a, const char *b)
{
    unsigned int n = 0;
    for (const char *p = a; p && *p && n + 1 < sizeof(r->error); p++) r->error[n++] = *p;
    for (const char *p = b; p && *p && n + 1 < sizeof(r->error); p++) r->error[n++] = *p;
    r->error[n] = 0;
}
static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static int prefix_ci(const char *s, const char *prefix)
{
    while (*prefix) if (lower(*s++) != lower(*prefix++)) return 0;
    return 1;
}
static void append(char *out, unsigned int capacity, unsigned int *n, const char *text)
{
    while (*text && *n + 1 < capacity) out[(*n)++] = *text++;
    out[*n] = 0;
}
static void append_number(char *out, unsigned int capacity, unsigned int *n, unsigned int value)
{
    char digits[12]; int k = 0;
    do digits[k++] = (char)('0' + value % 10); while (value /= 10);
    while (k && *n + 1 < capacity) out[(*n)++] = digits[--k];
    out[*n] = 0;
}

typedef struct { int secure; char host[256]; unsigned int port; char path[1024]; } Url;
static int parse_url(const char *text, Url *u)
{
    if (prefix_ci(text, "https://")) { u->secure = 1; u->port = 443; text += 8; }
    else if (prefix_ci(text, "http://")) { u->secure = 0; u->port = 80; text += 7; }
    else return -1;
    unsigned int n = 0;
    while (*text && *text != '/' && *text != ':' && *text != '?' && *text != '#') {
        if (n + 1 >= sizeof(u->host)) return -1;
        u->host[n++] = *text++;
    }
    u->host[n] = 0;
    if (!n) return -1;
    if (*text == ':') {
        unsigned int port = 0;
        for (text++; *text >= '0' && *text <= '9'; text++) port = port * 10 + (unsigned int)(*text - '0');
        if (!port || port > 65535) return -1;
        u->port = port;
    }
    n = 0;
    if (*text != '/') u->path[n++] = '/';
    while (*text && *text != '#') {
        if (n + 1 >= sizeof(u->path)) return -1;
        unsigned char c = (unsigned char)*text++;
        if (c == ' ' || c >= 0x80) {   /* percent-encode what cannot go raw */
            if (n + 4 >= sizeof(u->path)) return -1;
            u->path[n++] = '%'; u->path[n++] = "0123456789ABCDEF"[c >> 4]; u->path[n++] = "0123456789ABCDEF"[c & 15];
        } else u->path[n++] = (char)c;
    }
    u->path[n] = 0;
    return 0;
}

static int raw_send(const void *data, unsigned int length)
{
    if (stream.secure) return tls_write(&tls, data, length) == (int)length ? 0 : -1;
    const uint8_t *p = data;
    while (length) {
        int sent = bnet_platform_send(stream.handle, p, length);
        if (sent <= 0) return -1;
        p += sent; length -= (unsigned int)sent;
    }
    return 0;
}
/* Refills the buffer: 1 more data, 0 end, -1 error. */
static int fill(void)
{
    if (stream.start == stream.end) stream.start = stream.end = 0;
    if (stream.end == sizeof(stream.buffer)) {
        memmove(stream.buffer, stream.buffer + stream.start, stream.end - stream.start);
        stream.end -= stream.start; stream.start = 0;
    }
    int got = stream.secure ? tls_read(&tls, stream.buffer + stream.end, sizeof(stream.buffer) - stream.end, 30000)
                            : bnet_platform_recv(stream.handle, stream.buffer + stream.end, sizeof(stream.buffer) - stream.end, 30000);
    if (got < 0) return -1;
    if (got == 0) return 0;
    stream.end += (unsigned int)got;
    return 1;
}
/* One header line without CR LF; -1 at the end of the stream. */
static int read_line(char *out, unsigned int capacity)
{
    unsigned int n = 0;
    for (;;) {
        while (stream.start < stream.end) {
            char c = (char)stream.buffer[stream.start++];
            if (c == '\n') { if (n && out[n - 1] == '\r') n--; out[n] = 0; return (int)n; }
            if (n + 1 < capacity) out[n++] = c;
        }
        if (fill() <= 0) return -1;
    }
}
static int deliver(BnetRequest *r, unsigned int length)
{
    int stop = r->data ? r->data(r->context, stream.buffer + stream.start, length) : 0;
    stream.start += length;
    r->received += length;
    return stop;
}
/* Body of exactly length bytes (or up to the end when length < 0). */
static int body(BnetRequest *r, long long length)
{
    while (length != 0) {
        if (stream.start == stream.end) {
            int got = fill();
            if (got < 0) { set_error(r, "the connection broke during the download", 0); return -1; }
            if (got == 0) {
                if (length < 0) return 0;
                set_error(r, "the server closed the connection early", 0); return -1;
            }
        }
        unsigned int part = stream.end - stream.start;
        if (length > 0 && part > length) part = (unsigned int)length;
        if (deliver(r, part)) { set_error(r, "stopped", 0); return -1; }
        if (length > 0) length -= part;
    }
    return 0;
}
static long long hex_value(const char *s)
{
    long long v = 0; int digits = 0;
    for (; *s; s++, digits++) {
        int c = lower(*s), d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        if (d < 0) break;
        v = v * 16 + d;
    }
    return digits ? v : -1;
}
static void close_stream(void)
{
    if (stream.secure) tls_close(&tls);
    if (stream.handle >= 0) bnet_platform_close(stream.handle);
    stream.handle = -1;
    kept.open = 0;
}
void bnet_close_idle(void) { if (kept.open) close_stream(); }
/* A redirect's small body is read, so the connection can serve the next request. */
static int discard(long long length)
{
    while (length > 0) {
        if (stream.start == stream.end && fill() <= 0) return -1;
        unsigned int part = stream.end - stream.start;
        if (part > length) part = (unsigned int)length;
        stream.start += part; length -= part;
    }
    return 0;
}

static int same_host(const char *a, const char *b)
{
    while (*a && lower(*a) == lower(*b)) { a++; b++; }
    return *a == *b;
}
static void keep(const Url *u)
{
    unsigned int n = 0;
    while (u->host[n] && n + 1 < sizeof(kept.host)) { kept.host[n] = u->host[n]; n++; }
    kept.host[n] = 0;
    kept.port = u->port; kept.secure = u->secure; kept.open = 1;
}
static int one_request(const Url *u, BnetRequest *r, char *location, unsigned int location_capacity)
{
    char error[96] = "";
    int reused = kept.open && kept.secure == u->secure && kept.port == u->port && same_host(kept.host, u->host);
    if (kept.open && !reused) close_stream();   /* kept for another server */
    kept.open = 0;
    stream.start = stream.end = 0;
    if (reused) goto connected;
    stream.secure = 0;
    stream.handle = bnet_platform_open(u->host, u->port, error, sizeof(error));
    if (stream.handle < 0) { set_error(r, error[0] ? error : "cannot connect", 0); return -1; }
    if (u->secure) {
        uint8_t random[96];
        char now[15];
        size_t roots_length;
        const uint8_t *roots = bnet_platform_roots(&roots_length);
        if (!roots) { close_stream(); set_error(r, "no trusted certificates (/etc/ssl/roots)", 0); return -1; }
        bnet_platform_random(random, sizeof(random));
        tls.context = &stream.handle; tls.send = tls_send; tls.recv = tls_recv;
        int dated = !bnet_platform_now(now);
        if (tls_connect(&tls, u->host, random, roots, roots_length, dated ? now : 0)) {
            set_error(r, "secure connection failed: ", tls.error);
            bnet_platform_close(stream.handle); stream.handle = -1;
            return -1;
        }
        stream.secure = 1;
    }
connected:;
    static char request[6144];
    unsigned int n = 0;
    append(request, sizeof(request), &n, r->method ? r->method : "GET");
    append(request, sizeof(request), &n, " ");
    append(request, sizeof(request), &n, u->path);
    append(request, sizeof(request), &n, " HTTP/1.1\r\nHost: ");
    append(request, sizeof(request), &n, u->host);
    if (u->port != (u->secure ? 443U : 80U)) { append(request, sizeof(request), &n, ":"); append_number(request, sizeof(request), &n, u->port); }
    append(request, sizeof(request), &n, "\r\nUser-Agent: ");
    append(request, sizeof(request), &n, r->user_agent ? r->user_agent : "Barnix/0.3 (git/2.0)");
    append(request, sizeof(request), &n, "\r\nAccept: ");
    append(request, sizeof(request), &n, r->accept ? r->accept : "*/*");
    append(request, sizeof(request), &n, r->accept_gzip ? "\r\nAccept-Encoding: gzip" : "\r\nAccept-Encoding: identity");
    append(request, sizeof(request), &n, r->keep_alive ? "\r\nConnection: keep-alive\r\n" : "\r\nConnection: close\r\n");
    if (r->request_headers) {
        const char *extra = r->request_headers(r->context, r->url);
        if (extra) append(request, sizeof(request), &n, extra);
    }
    if (r->body || (r->method && r->method[0] == 'P')) {
        append(request, sizeof(request), &n, "Content-Type: ");
        append(request, sizeof(request), &n, r->content_type ? r->content_type : "application/octet-stream");
        append(request, sizeof(request), &n, "\r\nContent-Length: ");
        append_number(request, sizeof(request), &n, r->body_length);
        append(request, sizeof(request), &n, "\r\n");
    }
    append(request, sizeof(request), &n, "\r\n");
    if (raw_send(request, n) || (r->body_length && raw_send(r->body, r->body_length))) {
        close_stream();
        if (reused) return one_request(u, r, location, location_capacity);   /* the server dropped the idle connection */
        set_error(r, "cannot send the request", 0); return -1;
    }
    static char line[8192];   /* long Set-Cookie and CSP lines */
    int status, first = 1;
    do {   /* skip 1xx interim answers */
        if (read_line(line, sizeof(line)) < 0 || !prefix_ci(line, "HTTP/1.")) {
            close_stream();
            if (reused && first) return one_request(u, r, location, location_capacity);
            set_error(r, "the server did not answer with HTTP", 0); return -1;
        }
        first = 0;
        status = 0;
        for (const char *p = line + 9; *p >= '0' && *p <= '9'; p++) status = status * 10 + (*p - '0');
        if (status >= 100 && status < 200)
            while (read_line(line, sizeof(line)) > 0) {}
    } while (status >= 100 && status < 200);
    r->status = status;
    char reason[64] = "";
    { unsigned int k = 0; for (const char *p = line + 13; *p && k + 1 < sizeof(reason); p++) reason[k++] = *p; reason[k] = 0; }
    long long length = -1;
    int chunked = 0, reusable = r->keep_alive && prefix_ci(line, "HTTP/1.1");
    location[0] = 0;
    r->response_encoding[0] = 0;
    for (;;) {
        int got = read_line(line, sizeof(line));
        if (got < 0) { close_stream(); set_error(r, "the server closed the connection in the headers", 0); return -1; }
        if (!got) break;
        if (r->response_header) r->response_header(r->context, r->url, line);
        if (prefix_ci(line, "content-type:")) {
            const char *p = line + 13;
            while (*p == ' ') p++;
            unsigned int k = 0;
            while (*p && k + 1 < sizeof(r->response_type)) r->response_type[k++] = *p++;
            r->response_type[k] = 0;
        }
        if (prefix_ci(line, "content-length:")) {
            length = 0;
            for (const char *p = line + 15; *p; p++) if (*p >= '0' && *p <= '9') length = length * 10 + (*p - '0');
        } else if (prefix_ci(line, "connection:")) {
            for (const char *p = line + 11; *p; p++) if (prefix_ci(p, "close")) reusable = 0;
        } else if (prefix_ci(line, "content-encoding:")) {
            const char *p = line + 17;
            while (*p == ' ') p++;
            unsigned int k = 0;
            while (*p && *p != ' ' && *p != ';' && k + 1 < sizeof(r->response_encoding)) r->response_encoding[k++] = (char)lower(*p++);
            r->response_encoding[k] = 0;
        } else if (prefix_ci(line, "transfer-encoding:")) {
            for (const char *p = line + 18; *p; p++) if (prefix_ci(p, "chunked")) chunked = 1;
        } else if (prefix_ci(line, "location:")) {
            const char *p = line + 9;
            while (*p == ' ') p++;
            unsigned int k = 0;
            while (*p && k + 1 < location_capacity) location[k++] = *p++;
            location[k] = 0;
        }
    }
    if (status >= 300 && status < 400 && location[0]) {
        if (reusable && !chunked && length >= 0 && length <= 65536 && !discard(length)) keep(u);
        else close_stream();
        return 1;
    }
    if (status < 200 || status >= 300) {
        char code[16]; unsigned int k = 0;
        append_number(code, sizeof(code), &k, (unsigned int)status);
        append(code, sizeof(code), &k, " ");
        set_error(r, "the server answered ", code);
        unsigned int e = (unsigned int)strlen(r->error);
        append(r->error, sizeof(r->error), &e, reason);
        close_stream();
        return -1;
    }
    r->length = chunked ? -1 : length;
    int result = 0;
    if (chunked) {
        for (;;) {
            if (read_line(line, sizeof(line)) < 0) { set_error(r, "the connection broke during the download", 0); result = -1; break; }
            long long size = hex_value(line);
            if (size < 0) { set_error(r, "damaged chunked body", 0); result = -1; break; }
            if (!size) { while (read_line(line, sizeof(line)) > 0) {} break; }
            if (body(r, size) || read_line(line, sizeof(line)) < 0) { result = -1; break; }
        }
    } else result = body(r, length);
    if (!result && reusable && (chunked || length >= 0)) keep(u);
    else close_stream();
    return result;
}

int bnet_request(const char *url, BnetRequest *r)
{
    static char location[1024];
    static Url u;
    r->status = 0; r->length = -1; r->received = 0; r->error[0] = 0; r->response_type[0] = 0;
    unsigned int n = 0;
    r->url[0] = 0;
    append(r->url, sizeof(r->url), &n, url);
    for (int redirects = 0; redirects <= 5; redirects++) {
        if (parse_url(r->url, &u)) { set_error(r, "not an http:// or https:// address: ", r->url); return -1; }
        int result = one_request(&u, r, location, sizeof(location));
        if (result <= 0) return result;
        /* Redirect: absolute, scheme-relative or path. */
        n = 0;
        if (prefix_ci(location, "http://") || prefix_ci(location, "https://")) append(r->url, sizeof(r->url), &n, location);
        else {
            append(r->url, sizeof(r->url), &n, u.secure ? "https:" : "http:");
            if (location[0] == '/' && location[1] == '/') append(r->url, sizeof(r->url), &n, location);
            else {
                append(r->url, sizeof(r->url), &n, "//");
                append(r->url, sizeof(r->url), &n, u.host);
                if (u.port != (u.secure ? 443U : 80U)) { append(r->url, sizeof(r->url), &n, ":"); append_number(r->url, sizeof(r->url), &n, u.port); }
                if (location[0] != '/') append(r->url, sizeof(r->url), &n, "/");
                append(r->url, sizeof(r->url), &n, location);
            }
        }
        if (r->status == 303) { r->method = "GET"; r->body = 0; r->body_length = 0; }
    }
    set_error(r, "too many redirects", 0);
    return -1;
}
