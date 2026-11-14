/* Barnix TCP (RFC 793/9293, client side): connect, in-order receive into a
 * 256 KiB buffer with negotiated RFC 7323 window scaling, send with go-back-N retransmission and exponential
 * backoff, FIN in both directions, RST. Out-of-order segments are dropped
 * and acknowledged, so the peer resends them. */
#include "net.h"
#include "barnix.h"

typedef unsigned char u8;
#define CONNECTIONS 4
#define RECV_SIZE (256U * 1024U)
#define RECEIVE_SCALE 2U
#define SEND_SIZE 32768U
enum { FREE, SYN_SENT, OPEN, CLOSED };
typedef struct {
    int state, error, fin_received, fin_queued, fin_sent;
    unsigned int remote, remote_port, local_port;
    unsigned int iss, snd_una, snd_nxt, snd_wnd, mss, rcv_nxt, advertised;
    unsigned int send_start, send_count, recv_start, recv_count;
    unsigned int timer, rto, retries;
    unsigned int window_scaled, peer_scale;
    u8 recv[RECV_SIZE], send[SEND_SIZE];
} Tcp;
static Tcp connections[CONNECTIONS];
static u8 segment[1500];

static void put16(u8 *p, unsigned int v) { p[0] = (u8)(v >> 8); p[1] = (u8)v; }
static void put32(u8 *p, unsigned int v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }
static unsigned int get16(const u8 *p) { return (unsigned int)p[0] << 8 | p[1]; }
static unsigned int get32(const u8 *p) { return (unsigned int)p[0] << 24 | (unsigned int)p[1] << 16 | (unsigned int)p[2] << 8 | p[3]; }
static unsigned int window(const Tcp *c) { unsigned int free = RECV_SIZE - c->recv_count; unsigned int limit = 65535u << (c->window_scaled ? RECEIVE_SCALE : 0); return free > limit ? limit : free; }

enum { FIN = 1, SYN = 2, RST = 4, PSH = 8, ACK = 16 };
static int emit(Tcp *c, unsigned int sequence, int flags, unsigned int data_offset, unsigned int length)
{
    unsigned int header = flags & SYN ? 28 : 20;
    put16(segment, c->local_port); put16(segment + 2, c->remote_port);
    put32(segment + 4, sequence); put32(segment + 8, flags & ACK ? c->rcv_nxt : 0);
    segment[12] = (u8)(header / 4 << 4); segment[13] = (u8)flags;
    c->advertised = window(c);
    unsigned int scale = !(flags & SYN) && c->window_scaled ? RECEIVE_SCALE : 0;
    unsigned int advertised = c->advertised >> scale;
    if (advertised > 65535) advertised = 65535;
    put16(segment + 14, advertised); put16(segment + 16, 0); put16(segment + 18, 0);
    if (flags & SYN) { segment[20] = 2; segment[21] = 4; put16(segment + 22, 1460); segment[24] = 1; segment[25] = 3; segment[26] = 3; segment[27] = RECEIVE_SCALE; }
    for (unsigned int i = 0; i < length; i++) segment[header + i] = c->send[(c->send_start + data_offset + i) % SEND_SIZE];
    u8 pseudo[12];
    put32(pseudo, net_address()); put32(pseudo + 4, c->remote); pseudo[8] = 0; pseudo[9] = 6; put16(pseudo + 10, header + length);
    unsigned int sum = net_checksum_add(net_checksum_add(0, pseudo, 12), segment, header + length);
    put16(segment + 16, net_checksum_end(sum));
    return net_ip_send(c->remote, 6, segment, header + length);
}
/* Sends what the peer's window allows, then the FIN; force: at least an ACK. */
static void output(Tcp *c, int force)
{
    if (c->state != OPEN) return;
    int sent = 0;
    unsigned int offset = c->snd_nxt - c->snd_una;
    if (c->fin_sent) offset--;   /* the FIN is not in the buffer */
    while (offset < c->send_count && offset < c->snd_wnd) {
        unsigned int length = c->send_count - offset;
        if (length > c->mss) length = c->mss;
        if (length > c->snd_wnd - offset) length = c->snd_wnd - offset;
        if (emit(c, c->snd_nxt, ACK | PSH, offset, length)) break;
        if (c->snd_nxt == c->snd_una) c->timer = net_ms();
        c->snd_nxt += length; offset += length; sent = 1;
    }
    if (c->fin_queued && !c->fin_sent && offset == c->send_count) {
        if (c->snd_nxt == c->snd_una) c->timer = net_ms();
        emit(c, c->snd_nxt, ACK | FIN, 0, 0);
        c->snd_nxt++; c->fin_sent = 1; sent = 1;
    }
    if (!sent && force) emit(c, c->snd_nxt, ACK, 0, 0);
}
void tcp_timers(void)
{
    for (int i = 0; i < CONNECTIONS; i++) {
        Tcp *c = &connections[i];
        if ((c->state != OPEN && c->state != SYN_SENT) || c->snd_una == c->snd_nxt) continue;
        if (net_ms() - c->timer < c->rto) continue;
        if (++c->retries > 8) { c->state = CLOSED; c->error = BARNIX_NET_TIMEOUT; continue; }
        c->rto = c->rto * 2 > 16000 ? 16000 : c->rto * 2;
        c->timer = net_ms();
        if (c->state == SYN_SENT) { emit(c, c->iss, SYN, 0, 0); continue; }
        c->snd_nxt = c->snd_una;   /* go back N */
        c->fin_sent = 0;
        output(c, 0);
    }
}
void tcp_input(unsigned int source, const u8 *p, unsigned int length)
{
    if (length < 20) return;
    unsigned int header = (unsigned int)(p[12] >> 4) * 4;
    if (header < 20 || header > length) return;
    u8 pseudo[12];
    put32(pseudo, source); put32(pseudo + 4, net_address()); pseudo[8] = 0; pseudo[9] = 6; put16(pseudo + 10, length);
    if (net_checksum_end(net_checksum_add(net_checksum_add(0, pseudo, 12), p, length))) return;
    unsigned int from = get16(p), to = get16(p + 2), sequence = get32(p + 4), ack = get32(p + 8);
    int flags = p[13];
    Tcp *c = 0;
    for (int i = 0; i < CONNECTIONS; i++)
        if (connections[i].state != FREE && connections[i].remote == source &&
            connections[i].remote_port == from && connections[i].local_port == to) c = &connections[i];
    if (!c || c->state == CLOSED) return;
    if (flags & RST) {
        if (c->state == SYN_SENT ? (flags & ACK) && ack == c->iss + 1 : sequence - c->rcv_nxt <= window(c)) {
            c->error = c->state == SYN_SENT ? BARNIX_NET_REFUSED : -1;
            c->state = CLOSED;
        }
        return;
    }
    if (c->state == SYN_SENT) {
        if ((flags & (SYN | ACK)) != (SYN | ACK) || ack != c->iss + 1) return;
        c->rcv_nxt = sequence + 1; c->snd_una = c->snd_nxt = ack;
        c->snd_wnd = get16(p + 14);
        c->mss = 536;
        for (unsigned int at = 20; at + 1 < header;) {   /* options: MSS */
            if (p[at] == 0) break;
            if (p[at] == 1) { at++; continue; }
            if (p[at] == 2 && p[at + 1] == 4 && at + 4 <= header) c->mss = get16(p + at + 2);
            if (p[at] == 3 && p[at + 1] == 3 && at + 3 <= header) { c->window_scaled = 1; c->peer_scale = p[at + 2] > 14 ? 14 : p[at + 2]; }
            if (p[at + 1] < 2 || at + p[at + 1] > header) break;
            at += p[at + 1];
        }
        if (c->mss > 1460) c->mss = 1460;
        if (c->mss < 64) c->mss = 64;
        c->state = OPEN; c->rto = 1000; c->retries = 0;
        emit(c, c->snd_nxt, ACK, 0, 0);
        return;
    }
    if (flags & ACK) {
        unsigned int acked = ack - c->snd_una, flight = c->snd_nxt - c->snd_una;
        if (acked && acked <= flight) {
            unsigned int data = acked > c->send_count ? c->send_count : acked;
            c->send_start = (c->send_start + data) % SEND_SIZE;
            c->send_count -= data;
            c->snd_una = ack;
            c->rto = 1000; c->retries = 0; c->timer = net_ms();
        }
        if (acked <= flight) c->snd_wnd = get16(p + 14) << (c->window_scaled ? c->peer_scale : 0);
    }
    unsigned int payload = length - header;
    int answer = 0;
    if (payload || (flags & FIN)) {
        unsigned int skip = c->rcv_nxt - sequence;   /* bytes we already have */
        if (skip <= payload && !c->fin_received) {
            unsigned int take = payload - skip, room = RECV_SIZE - c->recv_count;
            if (take > room) take = room;
            unsigned int at = (c->recv_start + c->recv_count) % RECV_SIZE;
            unsigned int first = RECV_SIZE - at; if (first > take) first = take;
            memcpy(c->recv + at, p + header + skip, first);
            memcpy(c->recv, p + header + skip + first, take - first);
            c->recv_count += take; c->rcv_nxt += take;
            if ((flags & FIN) && skip + take == payload) { c->rcv_nxt++; c->fin_received = 1; }
        }
        answer = 1;
    }
    output(c, answer);
}

static int handle_of(int handle, Tcp **out)
{
    if (handle < 0 || handle >= CONNECTIONS || connections[handle].state == FREE) return -1;
    *out = &connections[handle];
    return 0;
}
int net_open(const char *host, unsigned int port)
{
    unsigned int address;
    int problem = net_resolve(host, &address);
    if (problem) return problem;
    Tcp *c = 0;
    int handle = -1;
    for (int i = 0; i < CONNECTIONS && !c; i++) if (connections[i].state == FREE) { c = &connections[i]; handle = i; }
    if (!c) return BARNIX_NET_BUSY;
    unsigned int random[2];
    net_random(random, sizeof(random));
    memset(c, 0, (unsigned int)((u8 *)c->recv - (u8 *)c));
    c->remote = address; c->remote_port = port & 0xffff;
    c->local_port = 49152 + (random[0] & 0x3fff);
    c->iss = random[1]; c->snd_una = c->iss; c->snd_nxt = c->iss + 1;
    c->rto = 1000; c->timer = net_ms(); c->mss = 536;
    c->state = SYN_SENT;
    emit(c, c->iss, SYN, 0, 0);
    if (port & 0x10000) return handle;   /* asynchronous: net_recv(handle, 0, 0, ms) reports the state */
    for (unsigned int start = net_ms(); net_ms() - start < 20000;) {
        net_poll();
        if (c->state == OPEN) return handle;
        if (c->state == CLOSED) break;
    }
    int error = c->state == CLOSED && c->error ? c->error : BARNIX_NET_TIMEOUT;
    c->state = FREE;
    return error;
}
int net_send(int handle, const void *data, unsigned int length)
{
    Tcp *c;
    if (handle_of(handle, &c) || c->state != OPEN || c->fin_queued) return -1;
    const u8 *p = data;
    unsigned int left = length, idle = net_ms();
    while (left) {
        unsigned int room = SEND_SIZE - c->send_count, part = left < room ? left : room;
        for (unsigned int i = 0; i < part; i++) c->send[(c->send_start + c->send_count + i) % SEND_SIZE] = p[i];
        c->send_count += part; p += part; left -= part;
        if (part) idle = net_ms();
        output(c, 0);
        if (left) {
            net_poll();
            if (c->state != OPEN) return -1;
            if (net_ms() - idle > 60000) return -1;
        }
    }
    return (int)length;
}
int net_recv(int handle, void *data, unsigned int capacity, unsigned int timeout_ms)
{
    Tcp *c;
    if (handle_of(handle, &c)) return -1;
    if (!capacity) {   /* the state of an asynchronous connect: 1 open, -2 still connecting, -1 failed */
        for (unsigned int start = net_ms();;) {
            net_poll();
            if (c->state == OPEN || c->recv_count || c->fin_received) return 1;
            if (c->state == CLOSED) return -1;
            if (net_ms() - start >= timeout_ms) return -2;
        }
    }
    for (unsigned int start = net_ms();;) {
        net_poll();
        if (c->recv_count) {
            unsigned int n = c->recv_count < capacity ? c->recv_count : capacity;
            u8 *out = data;
            unsigned int first = RECV_SIZE - c->recv_start; if (first > n) first = n;
            memcpy(out, c->recv + c->recv_start, first);
            memcpy(out + first, c->recv, n - first);
            c->recv_start = (c->recv_start + n) % RECV_SIZE;
            c->recv_count -= n;
            if (c->state == OPEN && c->advertised < 2 * c->mss && window(c) >= 2 * c->mss) output(c, 1);   /* window update */
            return (int)n;
        }
        if (c->fin_received) return 0;
        if (c->state == CLOSED) return c->error == BARNIX_NET_TIMEOUT ? -2 : -1;
        if (net_ms() - start >= timeout_ms) return -2;
    }
}
void net_close(int handle)
{
    Tcp *c;
    if (handle_of(handle, &c)) return;
    if (c->state == OPEN) {
        c->fin_queued = 1;
        output(c, 0);
        for (unsigned int start = net_ms(); net_ms() - start < 1000 && c->state == OPEN && c->snd_una != c->snd_nxt;) net_poll();
    }
    c->state = FREE;
}
void net_reset(void)
{
    for (int i = 0; i < CONNECTIONS; i++) connections[i].state = FREE;
}
