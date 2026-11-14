/* Barnix Net (BNET): HTTP and HTTPS for Barnix programs (ABI 12).
 *
 * The kernel provides Ethernet, DHCP, DNS and TCP; this library adds
 * HTTP/1.1 and TLS 1.3 with certificate checking against the roots in
 * /etc/ssl/roots. Link BDK/runtime/{net,http,tls,x509,pk,crypto}.c
 * (and inflate.c for bnet_inflate). */
#ifndef BARNIX_NET_H
#define BARNIX_NET_H
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *method;          /* "GET" (default) or "POST" */
    const char *content_type;    /* type of the POST body */
    const void *body;
    unsigned int body_length;
    const char *accept;          /* Accept header, optional */
    /* Every piece of the response body; return nonzero to stop. */
    int (*data)(void *context, const void *data, unsigned int length);
    void *context;
    /* Filled by bnet_request: */
    int status;                  /* HTTP status, 0 without an answer */
    long long length;            /* Content-Length, -1 when unknown */
    unsigned int received;       /* body bytes delivered */
    char error[128];             /* why it failed */
    char url[512];               /* the final URL after redirects */
    /* Optional, after the fields above (older programs leave them zero): */
    const char *user_agent;      /* default "Barnix/0.3 (git/2.0)" */
    /* Extra request header lines ("Name: value\r\n"...) for each URL,
     * redirects included: cookies. 0 or "" for none. */
    const char *(*request_headers)(void *context, const char *url);
    /* Every response header line ("Set-Cookie: ..."), with the URL it came from. */
    void (*response_header)(void *context, const char *url, const char *line);
    char response_type[96];      /* filled: the Content-Type of the answer */
    /* Keep the connection open for the next request to the same host and
     * port (HTTP keep-alive); bnet_close_idle closes it. */
    int keep_alive;
    /* Ask for "Content-Encoding: gzip": the body arrives compressed and
     * response_encoding says so (bnet_gunzip unpacks it). */
    int accept_gzip;
    char response_encoding[24];  /* filled: Content-Encoding, "" for none */
} BnetRequest;

/* http:// or https:// URL. Follows up to 5 redirects. Returns 0 when a 2xx
 * response arrived completely, -1 otherwise (request->error says why). */
int bnet_request(const char *url, BnetRequest *request);

/* Closes the connection a keep_alive request left open. */
void bnet_close_idle(void);

/* Called about every 30 ms while a request or TCP call waits for the
 * network, so a program can keep its screen and input alive; returning
 * nonzero stops the request ("stopped"). 0 turns it off. */
void bnet_set_idle(int (*idle)(void));

/* Raw TCP for other protocols: a handle or a negative error. */
int bnet_tcp_open(const char *host, unsigned int port);
int bnet_tcp_send(int handle, const void *data, unsigned int length);
int bnet_tcp_recv(int handle, void *data, unsigned int capacity, unsigned int timeout_ms);
void bnet_tcp_close(int handle);
/* Text for a negative bnet_tcp_open result. */
const char *bnet_error(int code);

/* zlib/deflate data (git objects, .gz bodies): bytes written or -1. */
long bnet_inflate(const void *in, unsigned long in_length, unsigned long *used, void *out, unsigned long capacity, int zlib);
/* A gzip body: its unpacked size (from the trailer, -1 when it is not gzip),
 * and unpacking into out: bytes written or -1. */
long bnet_gunzip_size(const void *in, unsigned long in_length);
long bnet_gunzip(const void *in, unsigned long in_length, void *out, unsigned long capacity);
#ifdef __cplusplus
}
#endif
#endif
