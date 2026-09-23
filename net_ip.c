#include "net.h"

typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32;
struct eth { u8 dst[6], src[6]; u16 type; } __attribute__((packed));
struct arp { u16 htype, ptype; u8 hlen, plen; u16 oper; u8 sha[6]; u32 spa; u8 tha[6]; u32 tpa; } __attribute__((packed));
struct ipv4 { u8 vhl, tos; u16 len, id, frag; u8 ttl, proto; u16 sum; u32 src, dst; } __attribute__((packed));
struct icmp { u8 type, code; u16 sum, id, seq; } __attribute__((packed));
struct udp { u16 src, dst, len, sum; } __attribute__((packed));

static u32 local_ip = 0x0F02000A; /* 10.0.2.15, network byte order */
static u32 gateway_ip = 0x0202000A; /* 10.0.2.2 */
static u8 local_mac[6]; static u8 gateway_mac[6]; static int have_arp;
static u16 swap16(u16 x) { return (u16)((x << 8) | (x >> 8)); }
static u16 checksum(const void *data, unsigned int length)
{
    const u8 *p = data; u32 sum = 0;
    while (length > 1) { sum += ((u16)p[0] << 8) | p[1]; p += 2; length -= 2; }
    if (length) sum += (u16)p[0] << 8;
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return (u16)~sum;
}
static void copy_mac(u8 *d, const u8 *s) { for (int i = 0; i < 6; i++) d[i] = s[i]; }
static void put32(u8 *p, u32 v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static u16 read16(const u8 *p) { return (u16)(((u16)p[0] << 8) | p[1]); }

static int send_arp(void)
{
    u8 frame[64]; struct eth *e = (struct eth *)frame; struct arp *a = (struct arp *)(frame + sizeof(*e));
    for (int i = 0; i < 6; i++) e->dst[i] = 0xff;
    copy_mac(e->src, local_mac); e->type = swap16(0x0806);
    a->htype = swap16(1); a->ptype = swap16(0x0800); a->hlen = 6; a->plen = 4; a->oper = swap16(1);
    copy_mac(a->sha, local_mac); a->spa = local_ip; for (int i = 0; i < 6; i++) a->tha[i] = 0; a->tpa = gateway_ip;
    return net_packet_send(frame, sizeof(*e) + sizeof(*a));
}
static int resolve_gateway(void)
{
    if (have_arp) return 0;
    if (net_mac(local_mac)) return -1;
    send_arp();
    u8 frame[1600];
    for (unsigned int wait = 0; wait < 300000; wait++) {
        int n = net_packet_receive(frame, sizeof(frame)); if (n < (int)(sizeof(struct eth) + sizeof(struct arp))) continue;
        struct eth *e = (struct eth *)frame; struct arp *a = (struct arp *)(frame + sizeof(*e));
        if (swap16(e->type) == 0x0806 && swap16(a->oper) == 2 && a->spa == gateway_ip) {
            copy_mac(gateway_mac, a->sha); have_arp = 1; return 0;
        }
    }
    return -1;
}

int net_ip_ping(unsigned int *milliseconds)
{
    if (resolve_gateway()) return -1;
    u8 frame[128]; struct eth *e = (struct eth *)frame; struct ipv4 *ip = (struct ipv4 *)(frame + sizeof(*e));
    struct icmp *icmp = (struct icmp *)(frame + sizeof(*e) + sizeof(*ip));
    copy_mac(e->dst, gateway_mac); copy_mac(e->src, local_mac); e->type = swap16(0x0800);
    ip->vhl = 0x45; ip->tos = 0; ip->len = swap16(sizeof(*ip) + sizeof(*icmp)); ip->id = 0; ip->frag = 0;
    ip->ttl = 64; ip->proto = 1; ip->sum = 0; ip->src = local_ip; ip->dst = gateway_ip; ip->sum = checksum(ip, sizeof(*ip));
    icmp->type = 8; icmp->code = 0; icmp->sum = 0; icmp->id = swap16(0xBADD); icmp->seq = swap16(1); icmp->sum = checksum(icmp, sizeof(*icmp));
    if (net_packet_send(frame, sizeof(*e) + sizeof(*ip) + sizeof(*icmp))) return -1;
    for (unsigned int wait = 0; wait < 500000; wait++) {
        int n = net_packet_receive(frame, sizeof(frame)); if (n < 42) continue;
        e = (struct eth *)frame; ip = (struct ipv4 *)(frame + sizeof(*e)); icmp = (struct icmp *)(frame + sizeof(*e) + sizeof(*ip));
        if (swap16(e->type) == 0x0800 && ip->proto == 1 && icmp->type == 0 && icmp->id == swap16(0xBADD)) {
            if (milliseconds) *milliseconds = wait / 1000 + 1;
            return 0;
        }
    }
    return -1;
}

int net_ip_dhcp(void)
{
    if (net_mac(local_mac)) return -1;
    static u32 xid = 0xBAA11001;
    u8 frame[400]; struct eth *e = (struct eth *)frame;
    struct ipv4 *ip = (struct ipv4 *)(frame + sizeof(*e));
    struct udp *u = (struct udp *)(frame + sizeof(*e) + sizeof(*ip));
    u8 *p = frame + sizeof(*e) + sizeof(*ip) + sizeof(*u);
    for (int i = 0; i < 6; i++) e->dst[i] = 0xff;
    copy_mac(e->src, local_mac); e->type = swap16(0x0800);
    p[0] = 1; p[1] = 1; p[2] = 6; p[3] = 0; put32(p + 4, xid); p[10] = 0x80;
    copy_mac(p + 28, local_mac); p[236] = 99; p[237] = 130; p[238] = 83; p[239] = 99;
    p[240] = 53; p[241] = 1; p[242] = 1; p[243] = 255;
    unsigned int plen = 244, ilen = sizeof(*ip) + sizeof(*u) + plen;
    ip->vhl = 0x45; ip->tos = 0; ip->len = swap16(ilen); ip->id = 0; ip->frag = 0;
    ip->ttl = 64; ip->proto = 17; ip->sum = 0; ip->src = 0; ip->dst = 0xffffffff; ip->sum = checksum(ip, sizeof(*ip));
    u->src = swap16(68); u->dst = swap16(67); u->len = swap16(sizeof(*u) + plen); u->sum = 0;
    if (net_packet_send(frame, sizeof(*e) + ilen)) return -1;
    for (unsigned int wait = 0; wait < 800000; wait++) {
        int n = net_packet_receive(frame, sizeof(frame)); if (n < 300) continue;
        e = (struct eth *)frame; ip = (struct ipv4 *)(frame + sizeof(*e));
        u = (struct udp *)(frame + sizeof(*e) + sizeof(*ip)); p = frame + sizeof(*e) + sizeof(*ip) + sizeof(*u);
        if (swap16(e->type) != 0x0800 || ip->proto != 17 || swap16(u->src) != 67) continue;
        if (p[0] != 2 || p[1] != 1 || p[2] != 6) continue;
        local_ip = ((u32)p[16] << 24) | ((u32)p[17] << 16) | ((u32)p[18] << 8) | p[19];
        for (unsigned int i = 240; i + 1 < (unsigned int)n - sizeof(*e) - sizeof(*ip) - sizeof(*u); ) {
            u8 tag = p[i++], len = p[i++]; if (tag == 255) break;
            if (i + len > (unsigned int)n) break;
            if (tag == 3 && len >= 4) gateway_ip = ((u32)p[i] << 24) | ((u32)p[i+1] << 16) | ((u32)p[i+2] << 8) | p[i+3];
            i += len;
        }
        have_arp = 0; return 0;
    }
    return -1;
}

int net_dns_resolve(const char *name, unsigned int *address)
{
    if (!name || !address || resolve_gateway()) return -1;
    u8 frame[512]; struct eth *e = (struct eth *)frame; struct ipv4 *ip = (struct ipv4 *)(frame + sizeof(*e));
    struct udp *u = (struct udp *)(frame + sizeof(*e) + sizeof(*ip)); u8 *p = frame + sizeof(*e) + sizeof(*ip) + sizeof(*u);
    copy_mac(e->dst, gateway_mac); copy_mac(e->src, local_mac); e->type = swap16(0x0800);
    p[0] = 0x42; p[1] = 0x21; p[2] = 1; p[3] = 0; p[4] = 0; p[5] = 1; p[6] = 0; p[7] = 0; p[8] = 0; p[9] = 0; p[10] = 0; p[11] = 0;
    unsigned int n = 12, label = 0, i = 0;
    while (name[i]) { if (name[i] == '.') { p[n++] = (u8)(i - label); while (label < i) p[n++] = (u8)name[label++]; label++; } i++; }
    p[n++] = (u8)(i - label); while (label < i) p[n++] = (u8)name[label++]; p[n++] = 0; p[n++] = 0; p[n++] = 1; p[n++] = 0; p[n++] = 1;
    unsigned int plen = n; ip->vhl = 0x45; ip->tos = 0; ip->len = swap16(sizeof(*ip) + sizeof(*u) + plen); ip->id = 0; ip->frag = 0; ip->ttl = 64; ip->proto = 17; ip->sum = 0; ip->src = local_ip; ip->dst = 0x0302000A; ip->sum = checksum(ip, sizeof(*ip));
    u->src = swap16(49152); u->dst = swap16(53); u->len = swap16(sizeof(*u) + plen); u->sum = 0;
    if (net_packet_send(frame, sizeof(*e) + sizeof(*ip) + sizeof(*u) + plen)) return -1;
    for (unsigned int wait = 0; wait < 700000; wait++) {
        int got = net_packet_receive(frame, sizeof(frame)); if (got < 60) continue;
        e = (struct eth *)frame; ip = (struct ipv4 *)(frame + sizeof(*e)); u = (struct udp *)(frame + sizeof(*e) + sizeof(*ip)); p = frame + sizeof(*e) + sizeof(*ip) + sizeof(*u);
        if (swap16(e->type) != 0x0800 || ip->proto != 17 || swap16(u->src) != 53 || read16(p) != 0x4221 || read16(p + 6) == 0) continue;
        unsigned int q = 12;
        while (q < (unsigned int)got && p[q]) q += p[q] + 1;
        q += 5;
        unsigned int answers = read16(p + 6);
        for (unsigned int a = 0; a < answers && q + 12 <= (unsigned int)got; a++) {
            if ((p[q] & 0xC0) == 0xC0) q += 2; else { while (q < (unsigned int)got && p[q]) q += p[q] + 1; q++; }
            if (q + 10 > (unsigned int)got) break;
            u16 type = read16(p + q); u16 len = read16(p + q + 8); q += 10;
            if (type == 1 && len == 4 && q + 4 <= (unsigned int)got) { *address = ((u32)p[q] << 24) | ((u32)p[q+1] << 16) | ((u32)p[q+2] << 8) | p[q+3]; return 0; }
            q += len;
        }
    }
    return -1;
}
