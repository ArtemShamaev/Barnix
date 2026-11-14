/* Barnix virtual Wi-Fi card for QEMU, which emulates no Wi-Fi hardware.
 * An Ethernet adapter whose MAC starts with 02:57:49:46:49 ("WIFI") is a
 * radio: each Ethernet frame of type 0x88b5 carries one 802.11 frame
 * (without FCS) after a signal byte (dBm). scripts/virtual_wifi.py is the
 * access point on the other side. The card is "full MAC light": it only
 * sends a probe request when asked to scan; everything else is wifi.c. */
#include "wifi.h"
#include "barnix.h"

#define TYPE 0x88b5
static NetDriver nic;
static unsigned int scan_start;
static unsigned char packet[2048];

static int tx(const void *frame, unsigned int length)
{
    if (length + 15 > 1792) return -1;   /* RTL8139 frames (net_realtek.c) */
    memset(packet, 0xff, 6);
    memcpy(packet + 6, nic.mac, 6);
    packet[12] = TYPE >> 8; packet[13] = TYPE & 255;
    packet[14] = 0;
    memcpy(packet + 15, frame, length);
    return nic.send(packet, length + 15);
}
static int rx(void *frame, unsigned int capacity, int *rssi)
{
    for (int n = 0; n < 8; n++) {
        int length = nic.receive(packet, sizeof(packet));
        if (length <= 0) return 0;
        if (length < 15 + 10 || packet[12] != (TYPE >> 8) || packet[13] != (TYPE & 255)) continue;
        if ((unsigned int)length - 15 > capacity) continue;
        if (rssi) *rssi = (signed char)packet[14];
        memcpy(frame, packet + 15, (unsigned int)length - 15);
        return length - 15;
    }
    return 0;
}
static int scan(void)
{
    static const unsigned char body[] = {0, 0, 1, 8, 0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24};
    unsigned char probe[24 + sizeof(body)];
    memset(probe, 0, 24);
    probe[0] = 0x40;   /* probe request */
    memset(probe + 4, 0xff, 6); memcpy(probe + 10, nic.mac, 6); memset(probe + 16, 0xff, 6);
    memcpy(probe + 24, body, sizeof(body));
    scan_start = net_ms();
    return tx(probe, sizeof(probe));
}
static int scanning(void) { return net_ms() - scan_start < 400; }
static int join(const WifiBss *bss) { (void)bss; return 0; }
static int associated(const WifiBss *bss, unsigned int aid) { (void)bss; (void)aid; return 0; }
static void leave(void) {}

int vwifi_probe(const NetDriver *ethernet)
{
    static const unsigned char prefix[5] = {0x02, 0x57, 0x49, 0x46, 0x49};
    if (memcmp(ethernet->mac, prefix, 5)) return -1;
    nic = *ethernet;
    WifiHw hw;
    memset(&hw, 0, sizeof(hw));
    strcpy(hw.name, "Barnix virtual Wi-Fi (QEMU)");
    memcpy(hw.mac, nic.mac, 6);
    hw.scan = scan; hw.scanning = scanning; hw.join = join; hw.associated = associated;
    hw.leave = leave; hw.tx = tx; hw.rx = rx;
    wifi_attach(&hw);
    return 0;
}
