/* Barnix Wi-Fi station (see wifi.h): scanning, open system authentication,
 * association, the WPA2-Personal 4-way handshake (and group key updates),
 * CCMP, and 802.11 <-> Ethernet framing for the TCP/IP stack. Polled like
 * the rest of the network code; written from IEEE 802.11-2020 clauses 9,
 * 11.3, 12.5.3 and 12.7. */
#include "wifi.h"
#include "wifi_crypto.h"
#include "net.h"

typedef unsigned char u8;
static void copy(void *to, const void *from, unsigned int n) { u8 *t = to; const u8 *f = from; while (n--) *t++ = *f++; }
static void zero(void *to, unsigned int n) { u8 *t = to; while (n--) *t++ = 0; }
static int equal(const void *a, const void *b, unsigned int n)
{
    const u8 *x = a, *y = b;
    u8 d = 0;
    while (n--) d |= *x++ ^ *y++;
    return d == 0;
}
static unsigned int text_length(const char *s) { unsigned int n = 0; while (s[n]) n++; return n; }
static void text_copy(char *to, const char *from, unsigned int capacity)
{
    unsigned int n = 0;
    while (from[n] && n + 1 < capacity) { to[n] = from[n]; n++; }
    to[n] = 0;
}
static unsigned int get16le(const u8 *p) { return p[0] | (unsigned int)p[1] << 8; }
static unsigned int get16(const u8 *p) { return (unsigned int)p[0] << 8 | p[1]; }
static void put16(u8 *p, unsigned int v) { p[0] = (u8)(v >> 8); p[1] = (u8)v; }

static WifiHw hw;
static int have_card, ready;
static char card[64], state_text[64];

/* The network we are in */
static WifiBss bss;
static int connected, protected_link;
static WifiAes tk, gtk[4];
static unsigned int gtk_valid;
static unsigned long long tx_pn, rx_pn, rx_gpn[4];
static unsigned int sequence;
static u8 kck[16], kek[16], pmk[32];
static unsigned long long replay;
static u8 frame[2400], out[2400];

static const u8 snap[6] = {0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00};
static const u8 broadcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
/* RSN element we send: WPA2, CCMP group and pairwise, PSK */
static const u8 rsn_ie[22] = {48, 20, 1, 0, 0x00, 0x0f, 0xac, 4, 1, 0, 0x00, 0x0f, 0xac, 4,
                              1, 0, 0x00, 0x0f, 0xac, 2, 0, 0};

/* ------------------------------------------------------------ cards */
/* Wi-Fi cards often found in PCs. None has a Barnix driver yet: they need
 * vendor firmware loaded first. The QEMU virtual card is net_vwifi.c. */
static const struct { unsigned int id; const char *name; } known[] = {
    {0x27238086, "Intel Wi-Fi 6 AX200"}, {0xa0f08086, "Intel Wi-Fi 6 AX201"}, {0x02f08086, "Intel Wi-Fi 6 AX201"},
    {0x27258086, "Intel Wi-Fi 6E AX210"}, {0x24fd8086, "Intel Wireless 8265"}, {0x24f38086, "Intel Wireless 8260"},
    {0x095a8086, "Intel Wireless 7265"}, {0x08b18086, "Intel Wireless 7260"}, {0x9df08086, "Intel Wireless-AC 9560"},
    {0x003e168c, "Qualcomm Atheros QCA6174"}, {0x0042168c, "Qualcomm Atheros QCA9377"}, {0x002b168c, "Atheros AR9285"},
    {0x0032168c, "Atheros AR9485"}, {0xc82110ec, "Realtek RTL8821CE"}, {0xb82210ec, "Realtek RTL8822BE"},
    {0xc82210ec, "Realtek RTL8822CE"}, {0x818810ec, "Realtek RTL8188EE"}, {0x43a014e4, "Broadcom BCM4360"},
    {0x43b114e4, "Broadcom BCM4352"}, {0x796114c3, "MediaTek MT7921"}, {0x060814c3, "MediaTek MT7921K"},
};
int wifi_identify(unsigned int pci, unsigned int id)
{
    (void)pci;
    if (have_card) return 1;
    have_card = 1;
    text_copy(card, "Wi-Fi card ", sizeof(card));
    for (unsigned int i = 0; i < sizeof(known) / sizeof(known[0]); i++)
        if (known[i].id == id) text_copy(card, known[i].name, sizeof(card));
    if (card[0] == 'W') {
        static const char hex[] = "0123456789abcdef";
        char *p = card + 11;
        for (int shift = 12; shift >= 0; shift -= 4) *p++ = hex[id >> shift & 15];
        *p++ = ':';
        for (int shift = 28; shift >= 16; shift -= 4) *p++ = hex[id >> shift & 15];
        *p = 0;
    }
    text_copy(state_text, "Barnix has no driver for this card yet", sizeof(state_text));
    return 1;
}
void wifi_attach(const WifiHw *card_hw)
{
    hw = *card_hw;
    have_card = ready = 1;
    text_copy(card, hw.name, sizeof(card));
    text_copy(state_text, "ready", sizeof(state_text));
}
const char *wifi_adapter(void) { return have_card ? card : 0; }
const char *wifi_status(void) { return state_text; }
int wifi_ready(void) { return ready; }
int wifi_connected(void) { return connected; }
const char *wifi_ssid(void) { return connected ? bss.ssid : 0; }

/* ------------------------------------------------------------ frames */
static int send_management(unsigned int subtype, const u8 *destination, const u8 *body, unsigned int length)
{
    u8 *f = out;
    zero(f, 24);
    f[0] = (u8)(subtype << 4);
    copy(f + 4, destination, 6); copy(f + 10, hw.mac, 6); copy(f + 16, bss.bssid, 6);
    sequence = (sequence + 1) & 0xfff;
    f[22] = (u8)(sequence << 4); f[23] = (u8)(sequence >> 4);
    copy(f + 24, body, length);
    return hw.tx(f, 24 + length);
}
/* Fills b from a beacon or probe response body. */
static void parse_bss(const u8 *f, unsigned int length, int rssi, WifiBss *b)
{
    zero(b, sizeof(*b));
    copy(b->bssid, f + 16, 6);
    b->rssi = (signed char)rssi;
    const u8 *body = f + 24;
    unsigned int size = length - 24;
    b->capability = (unsigned short)get16le(body + 10);
    int rsn = 0, wpa1 = 0, psk = 0, sae = 0, enterprise = 0, ccmp_pairwise = 0, ccmp_group = 0;
    for (unsigned int at = 12; at + 2 <= size;) {
        unsigned int tag = body[at], n = body[at + 1];
        const u8 *v = body + at + 2;
        if (at + 2 + n > size) break;
        if (tag == 0 && n <= 32) { copy(b->ssid, v, n); b->ssid[n] = 0; b->ssid_length = (u8)n; }
        if (tag == 1 && n <= 16) { copy(b->rates, v, n); b->rates_length = (u8)n; }
        if (tag == 50 && n <= 16) { copy(b->ext_rates, v, n); b->ext_rates_length = (u8)n; }
        if (tag == 3 && n >= 1) b->channel = v[0];
        if (tag == 221 && n >= 4 && v[0] == 0x00 && v[1] == 0x50 && v[2] == 0xf2 && v[3] == 1) wpa1 = 1;
        if (tag == 48 && n >= 8) {
            rsn = 1;
            ccmp_group = v[2] == 0x00 && v[3] == 0x0f && v[4] == 0xac && v[5] == 4;
            unsigned int p = 6, count = n >= 8 ? get16le(v + 6) : 0;
            p += 2;
            for (unsigned int i = 0; i < count && p + 4 <= n; i++, p += 4)
                if (v[p] == 0x00 && v[p + 1] == 0x0f && v[p + 2] == 0xac && v[p + 3] == 4) ccmp_pairwise = 1;
            count = p + 2 <= n ? get16le(v + p) : 0;
            p += 2;
            for (unsigned int i = 0; i < count && p + 4 <= n; i++, p += 4) {
                if (v[p] != 0x00 || v[p + 1] != 0x0f || v[p + 2] != 0xac) continue;
                if (v[p + 3] == 2 || v[p + 3] == 6) psk = 1;
                else if (v[p + 3] == 8) sae = 1;
                else if (v[p + 3] == 1 || v[p + 3] == 5) enterprise = 1;
            }
        }
        at += 2 + n;
    }
    if (!(b->capability & 0x10)) b->security = WIFI_OPEN;
    else if (rsn && psk && ccmp_pairwise && ccmp_group) b->security = WIFI_WPA2;
    else {
        b->security = WIFI_UNSUPPORTED;
        text_copy(b->why, rsn && sae ? "WPA3" : rsn && enterprise ? "Enterprise" : rsn && psk ? "TKIP" :
                          wpa1 ? "WPA" : rsn ? "RSN" : "WEP", sizeof(b->why));
    }
}
static int same_mac(const u8 *a, const u8 *b) { return equal(a, b, 6); }

/* ------------------------------------------------------------ scan */
static int scan_into(WifiBss *list, int max)
{
    if (!have_card) return BARNIX_NET_NO_ADAPTER;
    if (!ready) return BARNIX_NET_NO_DRIVER;
    if (hw.scan()) return BARNIX_NET_NO_DRIVER;
    int count = 0;
    unsigned int start = net_ms();
    for (int last = 0; !last;) {
        last = !hw.scanning() || net_ms() - start > 8000;
        int rssi, length;
        while ((length = hw.rx(frame, sizeof(frame), &rssi)) > 0) {
            unsigned int subtype = frame[0] >> 4;
            if (length < 24 + 12 || (frame[0] & 0x0c) != 0 || (subtype != 8 && subtype != 5)) continue;
            WifiBss b;
            parse_bss(frame, (unsigned int)length, rssi, &b);
            if (!b.ssid_length) continue;   /* hidden network */
            int i = 0;
            while (i < count && !same_mac(list[i].bssid, b.bssid)) i++;
            if (i < count) { if (b.rssi > list[i].rssi) list[i].rssi = b.rssi; continue; }
            if (count < max) list[count++] = b;
        }
    }
    return count;
}
int wifi_scan(WifiBss *list, int max)
{
    return scan_into(list, max);
}

/* ------------------------------------------------------------ joining */
/* Waits for a management frame of subtype from the AP: body length, 0 on
 * timeout, -1 when the AP threw us out. */
static int wait_management(unsigned int subtype, unsigned int timeout_ms)
{
    for (unsigned int start = net_ms(); net_ms() - start < timeout_ms;) {
        int rssi, length = hw.rx(frame, sizeof(frame), &rssi);
        if (length < 24 || (frame[0] & 0x0c) != 0) continue;
        if (!same_mac(frame + 10, bss.bssid) || !same_mac(frame + 4, hw.mac)) continue;
        unsigned int got = frame[0] >> 4;
        if (got == 10 || got == 12) return -1;
        if (got == subtype) return length - 24;
    }
    return 0;
}
static int authenticate(void)
{
    static const u8 request[6] = {0, 0, 1, 0, 0, 0};   /* open system, sequence 1, status 0 */
    for (int attempt = 0; attempt < 3; attempt++) {
        if (send_management(11, bss.bssid, request, 6)) continue;
        int n = wait_management(11, 500);
        if (n < 0) return BARNIX_NET_REJECTED;
        if (n >= 6 && get16le(frame + 26) == 2) return get16le(frame + 28) ? BARNIX_NET_REJECTED : 0;
    }
    return BARNIX_NET_TIMEOUT;
}
static int associate(unsigned int *aid)
{
    static u8 request[128];
    unsigned int n = 0;
    unsigned int capability = 0x0001 | (bss.capability & 0x0020) | (protected_link ? 0x0010 : 0);
    request[n++] = (u8)capability; request[n++] = (u8)(capability >> 8);
    request[n++] = 10; request[n++] = 0;   /* listen interval */
    request[n++] = 0; request[n++] = bss.ssid_length; copy(request + n, bss.ssid, bss.ssid_length); n += bss.ssid_length;
    if (bss.rates_length) { request[n++] = 1; request[n++] = bss.rates_length; copy(request + n, bss.rates, bss.rates_length); n += bss.rates_length; }
    else { static const u8 basic[6] = {1, 4, 0x82, 0x84, 0x8b, 0x96}; copy(request + n, basic, 6); n += 6; }
    if (bss.ext_rates_length) { request[n++] = 50; request[n++] = bss.ext_rates_length; copy(request + n, bss.ext_rates, bss.ext_rates_length); n += bss.ext_rates_length; }
    if (protected_link) { copy(request + n, rsn_ie, sizeof(rsn_ie)); n += sizeof(rsn_ie); }
    for (int attempt = 0; attempt < 3; attempt++) {
        if (send_management(0, bss.bssid, request, n)) continue;
        int got = wait_management(1, 700);
        if (got < 0) return BARNIX_NET_REJECTED;
        if (got >= 6) {
            if (get16le(frame + 26)) return BARNIX_NET_REJECTED;
            *aid = get16le(frame + 28) & 0x3fff;
            return 0;
        }
    }
    return BARNIX_NET_TIMEOUT;
}

/* ------------------------------------------------------------ EAPOL-Key */
/* Offsets inside an EAPOL-Key frame (802.1X header included) */
enum { K_INFO = 5, K_LENGTH = 7, K_REPLAY = 9, K_NONCE = 17, K_IV = 49, K_RSC = 65, K_MIC = 81, K_DATA_LENGTH = 97, K_DATA = 99 };
enum { KI_VERSION = 7, KI_PAIRWISE = 1 << 3, KI_INSTALL = 1 << 6, KI_ACK = 1 << 7, KI_MIC = 1 << 8,
       KI_SECURE = 1 << 9, KI_ENCRYPTED = 1 << 12 };
static int send_data(const u8 *destination, unsigned int type, const u8 *payload, unsigned int length);
static unsigned long long get64(const u8 *p) { unsigned long long v = 0; for (int i = 0; i < 8; i++) v = v << 8 | p[i]; return v; }
static void eapol_mic(u8 *eapol, unsigned int length)
{
    u8 digest[20];
    zero(eapol + K_MIC, 16);
    wifi_hmac_sha1(kck, 16, eapol, length, digest);
    copy(eapol + K_MIC, digest, 16);
}
static int eapol_mic_ok(const u8 *eapol, unsigned int length)
{
    static u8 check[600];
    u8 digest[20];
    if (length > sizeof(check)) return 0;
    copy(check, eapol, length);
    zero(check + K_MIC, 16);
    wifi_hmac_sha1(kck, 16, check, length, digest);
    return equal(digest, eapol + K_MIC, 16);
}
/* A reply from the station: info, replay counter of the request, nonce, key data. */
static int eapol_reply(const u8 *request, unsigned int info, const u8 *nonce, const u8 *data, unsigned int data_length)
{
    static u8 reply[200];
    unsigned int length = K_DATA + data_length;
    zero(reply, length);
    reply[0] = request[0] ? request[0] : 1; reply[1] = 3; put16(reply + 2, length - 4);
    reply[4] = request[4];
    put16(reply + K_INFO, info);
    copy(reply + K_REPLAY, request + K_REPLAY, 8);
    if (nonce) copy(reply + K_NONCE, nonce, 32);
    put16(reply + K_DATA_LENGTH, data_length);
    copy(reply + K_DATA, data, data_length);
    eapol_mic(reply, length);
    return send_data(bss.bssid, 0x888e, reply, length);
}
/* The GTK KDE of decrypted key data: installs it. */
static int take_gtk(const u8 *data, unsigned int length, const u8 *rsc)
{
    for (unsigned int at = 0; at + 2 <= length;) {
        unsigned int tag = data[at], n = data[at + 1];
        if (tag == 0xdd && n == 0 && at + 2 == length) break;   /* padding */
        if (at + 2 + n > length) break;
        const u8 *v = data + at + 2;
        if (tag == 0xdd && n >= 6 + 16 && v[0] == 0x00 && v[1] == 0x0f && v[2] == 0xac && v[3] == 1) {
            unsigned int id = v[4] & 3;
            wifi_aes_key(&gtk[id], v + 6);
            gtk_valid |= 1u << id;
            rx_gpn[id] = 0;
            for (int i = 5; i >= 0; i--) rx_gpn[id] = rx_gpn[id] << 8 | rsc[i];
            return 0;
        }
        at += 2 + n;
    }
    return -1;
}
static int unwrap_key_data(const u8 *eapol, unsigned int length, u8 *plain, unsigned int *plain_length)
{
    unsigned int n = get16(eapol + K_DATA_LENGTH);
    if (K_DATA + n > length || n < 24 || n % 8 || n > 512) return -1;
    if (wifi_aes_unwrap(kek, eapol + K_DATA, n, plain)) return -1;
    *plain_length = n - 8;
    return 0;
}
/* Group key handshake while connected (the AP changes the GTK). */
static void group_key_update(const u8 *eapol, unsigned int length)
{
    static u8 plain[512];
    unsigned int plain_length, info = get16(eapol + K_INFO);
    if (length < K_DATA || (info & KI_PAIRWISE) || !(info & KI_MIC) || !(info & KI_ACK)) return;
    if (get64(eapol + K_REPLAY) <= replay || !eapol_mic_ok(eapol, length)) return;
    if (unwrap_key_data(eapol, length, plain, &plain_length) || take_gtk(plain, plain_length, eapol + K_RSC)) return;
    replay = get64(eapol + K_REPLAY);
    eapol_reply(eapol, 2 | KI_MIC | KI_SECURE, 0, 0, 0);
}

/* ------------------------------------------------------------ data frames */
static int send_data(const u8 *destination, unsigned int type, const u8 *payload, unsigned int length)
{
    u8 *f = out;
    if (length > 2300) return -1;
    zero(f, 24);
    f[0] = 0x08; f[1] = 0x01;   /* data, to the distribution system */
    copy(f + 4, bss.bssid, 6); copy(f + 10, hw.mac, 6); copy(f + 16, destination, 6);
    sequence = (sequence + 1) & 0xfff;
    f[22] = (u8)(sequence << 4); f[23] = (u8)(sequence >> 4);
    static u8 body[2400];
    copy(body, snap, 6); put16(body + 6, type); copy(body + 8, payload, length);
    length += 8;
    int keyed = protected_link && type != 0x888e;
    if (!keyed) { copy(f + 24, body, length); return hw.tx(f, 24 + length); }
    f[1] |= 0x40;
    unsigned long long pn = ++tx_pn;
    u8 *ccmp = f + 24;
    ccmp[0] = (u8)pn; ccmp[1] = (u8)(pn >> 8); ccmp[2] = 0; ccmp[3] = 0x20;
    for (int i = 0; i < 4; i++) ccmp[4 + i] = (u8)(pn >> (16 + 8 * i));
    wifi_ccmp_encrypt(&tk, f, 24, pn, body, length, f + 32);
    return hw.tx(f, 32 + length + 8);
}
/* One received data frame as Ethernet into ethernet (capacity bytes):
 * its length, 0 when it carried nothing for the stack. EAPOL goes to
 * eapol (when not 0) instead. */
static int data_input(int length, u8 *ethernet, unsigned int capacity, u8 **eapol, unsigned int *eapol_length)
{
    unsigned int fc0 = frame[0], fc1 = frame[1];
    if ((fc0 & 0x0c) != 0x08 || (fc0 & 0x40) || (fc1 & 3) != 2) return 0;   /* data from the AP, not null */
    if (!same_mac(frame + 10, bss.bssid)) return 0;
    int group = frame[4] & 1;
    if (!group && !same_mac(frame + 4, hw.mac)) return 0;
    unsigned int header = (fc0 & 0x80) ? 26 : 24;
    if ((unsigned int)length < header + 8) return 0;
    u8 *body = frame + header;
    unsigned int size = (unsigned int)length - header;
    static u8 plain[2400];
    if (fc1 & 0x40) {
        if (!protected_link || size < 16 + 8) return 0;
        unsigned long long pn = body[0] | (unsigned long long)body[1] << 8 | (unsigned long long)body[4] << 16 |
                                (unsigned long long)body[5] << 24 | (unsigned long long)body[6] << 32 | (unsigned long long)body[7] << 40;
        unsigned int id = body[3] >> 6;
        WifiAes *key = &tk;
        unsigned long long *last = &rx_pn;
        if (group) { if (!(gtk_valid & 1u << id)) return 0; key = &gtk[id]; last = &rx_gpn[id]; }
        if (pn <= *last && *last) return 0;   /* replay */
        if (wifi_ccmp_decrypt(key, frame, header, pn, body + 8, size - 8, plain)) return 0;
        *last = pn;
        body = plain; size -= 16;
    } else if (protected_link) {
        if (size < 8 || !equal(body, snap, 6) || get16(body + 6) != 0x888e) return 0;   /* only EAPOL in the clear */
    }
    if (size < 8 || !equal(body, snap, 6)) return 0;
    unsigned int type = get16(body + 6);
    if (type == 0x888e) {
        if (eapol) { *eapol = body + 8; *eapol_length = size - 8; }
        else group_key_update(body + 8, size - 8);
        return 0;
    }
    if (size - 8 + 14 > capacity) return 0;
    copy(ethernet, frame + 4, 6);        /* destination */
    copy(ethernet + 6, frame + 16, 6);   /* source */
    put16(ethernet + 12, type);
    copy(ethernet + 14, body + 8, size - 8);
    return (int)(size - 8 + 14);
}

/* ------------------------------------------------------------ WPA2 4-way handshake */
static int handshake(void)
{
    static u8 plain[512];
    u8 snonce[32], anonce[32], ptk[48];
    int have_ptk = 0;
    net_random(snonce, 32);
    replay = 0;
    unsigned int start = net_ms();
    while (net_ms() - start < 4000) {
        int rssi, length = hw.rx(frame, sizeof(frame), &rssi);
        if (length <= 0) continue;
        if ((frame[0] & 0x0c) == 0 && same_mac(frame + 10, bss.bssid) && same_mac(frame + 4, hw.mac) &&
            ((frame[0] >> 4) == 10 || (frame[0] >> 4) == 12)) {
            unsigned int reason = length >= 26 ? get16le(frame + 24) : 0;
            return reason == 15 || reason == 2 || reason == 23 ? BARNIX_NET_WRONG_KEY : BARNIX_NET_REJECTED;
        }
        u8 *eapol = 0;
        unsigned int n = 0;
        data_input(length, frame, 0, &eapol, &n);   /* capacity 0: only EAPOL is wanted */
        if (!eapol || n < K_DATA || eapol[1] != 3 || (eapol[4] != 2 && eapol[4] != 254)) continue;
        unsigned int info = get16(eapol + K_INFO);
        if (!(info & KI_PAIRWISE) || !(info & KI_ACK)) continue;
        unsigned long long counter = get64(eapol + K_REPLAY);
        if (have_ptk && counter <= replay) continue;
        if (!(info & KI_MIC)) {   /* message 1: ANonce */
            copy(anonce, eapol + K_NONCE, 32);
            wifi_ptk(pmk, bss.bssid, hw.mac, anonce, snonce, ptk);
            copy(kck, ptk, 16); copy(kek, ptk + 16, 16);
            have_ptk = 1;
            replay = counter;
            eapol_reply(eapol, 2 | KI_PAIRWISE | KI_MIC, snonce, rsn_ie, sizeof(rsn_ie));
            start = net_ms();
            continue;
        }
        /* message 3: the keys */
        if (!have_ptk || !equal(eapol + K_NONCE, anonce, 32) || !eapol_mic_ok(eapol, n)) continue;
        if (!(info & KI_INSTALL) || !(info & KI_ENCRYPTED)) continue;
        unsigned int plain_length;
        if (unwrap_key_data(eapol, n, plain, &plain_length)) continue;
        replay = counter;
        eapol_reply(eapol, 2 | KI_PAIRWISE | KI_MIC | KI_SECURE, 0, 0, 0);
        wifi_aes_key(&tk, ptk + 32);
        tx_pn = rx_pn = 0;
        take_gtk(plain, plain_length, eapol + K_RSC);
        return 0;
    }
    return replay ? BARNIX_NET_WRONG_KEY : BARNIX_NET_TIMEOUT;
}

/* ------------------------------------------------------------ the NetDriver face */
static int face_send(const void *ethernet, unsigned int length)
{
    const u8 *e = ethernet;
    if (!connected || length < 14) return -1;
    return send_data(e, get16(e + 12), e + 14, length - 14) ? -1 : 0;
}
static int face_receive(void *ethernet, unsigned int capacity)
{
    if (!connected) return 0;
    for (int n = 0; n < 16; n++) {
        int rssi, length = hw.rx(frame, sizeof(frame), &rssi);
        if (length <= 0) return 0;
        if (length < 24) continue;
        if ((frame[0] & 0x0c) == 0) {
            unsigned int subtype = frame[0] >> 4;
            if ((subtype == 10 || subtype == 12) && same_mac(frame + 10, bss.bssid) &&
                (same_mac(frame + 4, hw.mac) || same_mac(frame + 4, broadcast))) {
                connected = 0;
                hw.leave();
                text_copy(state_text, "the access point disconnected us", sizeof(state_text));
                return 0;
            }
            if (subtype == 8 && same_mac(frame + 16, bss.bssid)) bss.rssi = (signed char)rssi;
            continue;
        }
        int got = data_input(length, ethernet, capacity, 0, 0);
        if (got > 0) return got;
    }
    return 0;
}
static int face_link(void) { return connected; }

/* ------------------------------------------------------------ connect */
void wifi_disconnect(void)
{
    if (connected) {
        static const u8 reason[2] = {3, 0};   /* leaving */
        send_management(12, bss.bssid, reason, 2);
        hw.leave();
    }
    connected = 0;
    gtk_valid = 0;
    zero(&tk, sizeof(tk)); zero(gtk, sizeof(gtk)); zero(kck, 16); zero(kek, 16); zero(pmk, 32);
    if (ready) text_copy(state_text, "not connected", sizeof(state_text));
}
int wifi_connect(const char *ssid, const char *password, NetDriver *driver)
{
    static WifiBss list[24];
    if (!have_card) return BARNIX_NET_NO_ADAPTER;
    if (!ready) return BARNIX_NET_NO_DRIVER;
    if (!ssid || !ssid[0] || text_length(ssid) > 32) return BARNIX_NET_NOT_FOUND;
    wifi_disconnect();
    int found = -1;
    for (int attempt = 0; attempt < 3 && found < 0; attempt++) {
        int count = scan_into(list, 24);
        if (count < 0) return count;
        for (int i = 0; i < count; i++)
            if (text_length(list[i].ssid) == text_length(ssid) && equal(list[i].ssid, ssid, text_length(ssid)) &&
                (found < 0 || list[i].rssi > list[found].rssi)) found = i;
    }
    if (found < 0) { text_copy(state_text, "network not found", sizeof(state_text)); return BARNIX_NET_NOT_FOUND; }
    bss = list[found];
    if (bss.security == WIFI_UNSUPPORTED) return BARNIX_NET_UNSUPPORTED;
    protected_link = bss.security == WIFI_WPA2;
    if (protected_link) {
        /* 802.11 asks for 8..63 characters; shorter ones still give a key,
         * so networks such as the QEMU test network with "root" work. */
        unsigned int length = password ? text_length(password) : 0;
        if (!length || length > 63) return BARNIX_NET_WRONG_KEY;
        text_copy(state_text, "computing the key", sizeof(state_text));
        wifi_psk(password, (const u8 *)bss.ssid, bss.ssid_length, pmk);
    }
    if (hw.join(&bss)) return BARNIX_NET_NO_DRIVER;
    text_copy(state_text, "authenticating", sizeof(state_text));
    int result = authenticate();
    unsigned int aid = 0;
    if (!result) { text_copy(state_text, "associating", sizeof(state_text)); result = associate(&aid); }
    if (!result && protected_link) { text_copy(state_text, "WPA2 handshake", sizeof(state_text)); result = handshake(); }
    if (!result) result = hw.associated(&bss, aid) ? BARNIX_NET_NO_DRIVER : 0;
    if (result) {
        static const u8 reason[2] = {3, 0};
        send_management(12, bss.bssid, reason, 2);
        hw.leave();
        zero(pmk, 32);
        text_copy(state_text, result == BARNIX_NET_WRONG_KEY ? "wrong password" : "connection failed", sizeof(state_text));
        return result;
    }
    connected = 1;
    text_copy(state_text, "connected", sizeof(state_text));
    zero(driver, sizeof(*driver));
    text_copy(driver->name, hw.name, sizeof(driver->name));
    copy(driver->mac, hw.mac, 6);
    driver->send = face_send; driver->receive = face_receive; driver->link = face_link;
    return 0;
}
