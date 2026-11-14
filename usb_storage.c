#include "usb_storage.h"
#include "usb_xhci.h"
#include "usb_time.h"
#include "usb_legacy.h"
#include "mouse.h"
#include "usb_hid.h"
#include "barnix.h"
#include <stdint.h>

/* UHCI DMA descriptors use physical addresses. Barnix has identity addressing
 * for low kernel memory; all DMA buffers are static and below 4 GiB. */
typedef struct { volatile uint32_t link, status, token, buffer; } TD;
typedef struct { volatile uint32_t link, element, reserved[2]; } QH;
#define USB_CONTROLLERS 8
typedef struct {
    uint32_t c_frames[1024] __attribute__((aligned(4096)));
    QH c_queue __attribute__((aligned(16)));
    unsigned short c_io;
} UsbController;
typedef struct {
    unsigned short d_port;
    unsigned int d_capacity, d_tag, d_generation;
    unsigned char d_address, d_ep0_size, d_bulk_in, d_bulk_out;
    unsigned short d_in_size,d_out_size;
    int d_legacy;
    unsigned char d_in_toggle, d_out_toggle, d_interface_number;
    HidMouse d_hid;
    int d_report_protocol;
    int d_ready, d_mouse, d_seen, d_pending;
    unsigned char d_low, d_mouse_ep, d_mouse_toggle, d_enum_retries;
    unsigned int d_retry_at;
    unsigned short d_mouse_size;
    /* Hubs: one level, as the Intel rate-matching hub behind EHCI needs.
     * Children live in HUB_CHILDREN extra entries after the root ports. */
    signed char d_parent;            /* hub entry, -1 on a root port */
    unsigned char d_hub_port, d_speed, d_hub, d_hub_ports, d_controller, d_linked;
    unsigned short d_hub_seen, d_vid, d_pid;
    unsigned char d_classes[4];      /* first interface classes, for listings */
    unsigned int d_scan_at;
    /* Mouse pipe on EHCI/OHCI (-1 none) and diagnostics for "devices". */
    signed char d_pipe;
    unsigned char d_error_run;
    unsigned int d_reports, d_errors, d_last_error;
    QH d_mouse_queue __attribute__((aligned(16)));
    TD d_mouse_td __attribute__((aligned(16)));
    unsigned char d_report[1024] __attribute__((aligned(16)));
} UsbDevice;
static UsbController controllers[USB_CONTROLLERS];
#define ROOT_DEVICES 32
#define HUB_CHILDREN 8
static UsbDevice devices[ROOT_DEVICES + HUB_CHILDREN];
static int root_count;
static unsigned int tsc_per_ms, generation_counter;
static int initialized, controller_count, detected_count, device_count, unit;
static unsigned int mouse_poll_count;
static int xhci_unit=-1;
static UsbController *controller = &controllers[0];
static UsbDevice *device = &devices[0];
static TD transfers[512] __attribute__((aligned(16)));
static unsigned char dma[4096] __attribute__((aligned(4096)));
#define frames (controller->c_frames)
#define queue (controller->c_queue)
#define io_base (controller->c_io)
#define root_port (device->d_port)
#define capacity (device->d_capacity)
#define tag (device->d_tag)
#define generation (device->d_generation)
#define address (device->d_address)
#define ep0_size (device->d_ep0_size)
#define bulk_in (device->d_bulk_in)
#define bulk_out (device->d_bulk_out)
#define in_size (device->d_in_size)
#define out_size (device->d_out_size)
#define in_toggle (device->d_in_toggle)
#define out_toggle (device->d_out_toggle)
#define interface_number (device->d_interface_number)
#define ready (device->d_ready)

#define ACTIVE (1U << 23)
#define ERRORS ((1U << 22) | (1U << 21) | (1U << 20) | (1U << 18) | (1U << 17))
#define PID_IN 0x69
#define PID_OUT 0xE1
#define PID_SETUP 0x2D

static unsigned short in16(unsigned short port)
{ unsigned short v; __asm__ volatile("inw %1,%0":"=a"(v):"Nd"(port)); return v; }
static unsigned int in32(unsigned short port)
{ unsigned int v; __asm__ volatile("inl %1,%0":"=a"(v):"Nd"(port)); return v; }
static void out8(unsigned short port, unsigned char v)
{ __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(port):"memory"); }
static void out16(unsigned short port, unsigned short v)
{ __asm__ volatile("outw %0,%1"::"a"(v),"Nd"(port):"memory"); }
static void out32(unsigned short port, unsigned int v)
{ __asm__ volatile("outl %0,%1"::"a"(v),"Nd"(port):"memory"); }

static int delay_ms(unsigned int milliseconds){return usb_delay_ms(milliseconds);}
static uint32_t pci_read(unsigned int bus, unsigned int slot, unsigned int fn, unsigned int reg)
{
    out32(0xCF8, 0x80000000U | (bus << 16) | (slot << 11) | (fn << 8) | (reg & ~3U));
    return in32(0xCFC);
}
static void pci_write16(unsigned int bus, unsigned int slot, unsigned int fn,
                        unsigned int reg, unsigned short value)
{
    out32(0xCF8, 0x80000000U | (bus << 16) | (slot << 11) | (fn << 8) | (reg & ~3U));
    out16(0xCFC + (reg & 2), value);
}
static int start_controller(void)
{
    if (initialized) return device_count ? 0 : -1;
    initialized = 1;
    int legacy_count=usb_legacy_init();
    for(int n=0;n<ROOT_DEVICES+HUB_CHILDREN;n++){devices[n].d_legacy=-1;devices[n].d_parent=-1;devices[n].d_pipe=-1;}
    for (unsigned int bus = 0; bus < 256; bus++)
        for (unsigned int slot = 0; slot < 32; slot++)
        {
            if ((pci_read(bus, slot, 0, 0) & 0xFFFF) == 0xFFFF) continue;
            unsigned int functions = (pci_read(bus, slot, 0, 12) & 0x800000) ? 8 : 1;
            for (unsigned int fn = 0; fn < functions; fn++)
            {
                if ((pci_read(bus, slot, fn, 8) >> 8) != 0x0C0300) continue;
                detected_count++;
                unsigned int bar = pci_read(bus, slot, fn, 0x20);
                if (!(bar & 1) || !(bar & 0xFFFC) || (bar & 0xFFFF0000)) continue;
                if (controller_count == USB_CONTROLLERS) continue;
                controller = &controllers[controller_count];
                io_base = bar & 0xFFFC;
                pci_write16(bus, slot, fn, 4, pci_read(bus, slot, fn, 4) | 5);
                /* Disable legacy SMI/trap routing and clear legacy status. */
                pci_write16(bus, slot, fn, 0xC0, 0x8F00);
                out16(io_base, 2);
                for (int i = 0; i < 100 && (in16(io_base) & 2); i++)
                    if (delay_ms(1)) return -1;
                if (in16(io_base) & 2) continue;
                out16(io_base + 4, 0);
                out16(io_base + 2, 0x3F);
                queue.link = 1; queue.element = 1;
                for(int p=1;p>=0;p--) {
                    UsbDevice *m=&devices[controller_count*2+p];
                    m->d_mouse_queue.link=queue.link;
                    m->d_mouse_queue.element=1;
                    queue.link=(uint32_t)&m->d_mouse_queue|2;
                }
                for (int i = 0; i < 1024; i++) frames[i] = (uint32_t)&queue | 2;
                out16(io_base + 6, 0);
                out32(io_base + 8, (uint32_t)frames);
                out8(io_base + 12, 64);
                out16(io_base, 0xC1); /* Run, configured, 64-byte packets. */
                if (delay_ms(2)) return -1;
                if (in16(io_base + 2) & 0x20) continue;
                for (int p = 0; p < 2; p++) {
                    devices[controller_count * 2 + p].d_port = io_base + 0x10 + p * 2;
                    out16(io_base + 0x10 + p * 2, in16(io_base + 0x10 + p * 2) & ~4U);
                }
                controller_count++;
            }
        }
    device_count=controller_count*2;
    for(int n=0;n<device_count;n++)devices[n].d_controller=(unsigned char)(n/2);
    for(int n=0;n<legacy_count&&device_count<ROOT_DEVICES;n++)devices[device_count++].d_legacy=n;
    root_count=device_count;
    /* Hub children get fixed entries, so USB disk names stay stable (none
     * without legacy controllers: xHCI disks then start at usb0). */
    for(int n=0;root_count&&n<HUB_CHILDREN;n++)devices[device_count++].d_parent=-2;
    controller = &controllers[0];
    return root_count ? 0 : -1;
}
int usb_storage_select(int index)
{
    if (index < 0 || index >= USB_STORAGE_MAX) return -1;
    start_controller();
    if(index>=device_count){
        xhci_unit=index-device_count;
        return usb_xhci_controller_count()?0:-1;
    }
    xhci_unit=-1;
    unit = index; device = &devices[index]; controller = &controllers[device->d_controller];
    return 0;
}
static int connected(void)
{
    if (device->d_parent >= 0 && !device->d_hub_port) return 0;
    unsigned short status = device->d_legacy>=0?usb_legacy_status(device->d_legacy):in16(root_port);
    return (status & 5) == 5 && !(status & 2);
}
int usb_storage_present(void)
{
    if(xhci_unit>=0)return usb_xhci_storage_present(xhci_unit);
    if (!ready || !connected()) { ready = 0; return 0; }
    return 1;
}
unsigned int usb_storage_sectors(void) { return xhci_unit>=0?usb_xhci_storage_sectors(xhci_unit):capacity; }
unsigned int usb_storage_generation(void) { return xhci_unit>=0?usb_xhci_storage_generation(xhci_unit):generation; }

static unsigned int route(void)
{
    if (device->d_parent < 0) return USB_ROUTE_ROOT;
    return USB_ROUTE(device->d_speed, devices[(int)device->d_parent].d_address, device->d_hub_port);
}
/* One synchronous packet chain, maximum 4096 bytes. Short IN packets stop
 * the chain; all descriptors are detached before their buffers can be reused. */
static int transfer(unsigned char pid, unsigned char endpoint, unsigned short max_packet,
                    unsigned char *toggle, void *buffer, unsigned int length)
{
    if(device->d_legacy>=0)return usb_legacy_transfer(device->d_legacy,address,endpoint,max_packet,pid,toggle,buffer,length,route());
    if (!connected() || !max_packet || length > sizeof(dma)) return -1;
    unsigned int count = length ? (length + max_packet - 1) / max_packet : 1;
    if (count > sizeof(transfers) / sizeof(transfers[0])) return -1;
    if (pid != PID_IN && length) memcpy(dma, buffer, length);
    unsigned int remaining = length, offset = 0;
    for (unsigned int i = 0; i < count; i++)
    {
        unsigned int packet = remaining > max_packet ? max_packet : remaining;
        transfers[i].link = i + 1 == count ? 1 : (uint32_t)&transfers[i + 1] | 4;
        transfers[i].status = ((uint32_t)device->d_low<<26) | ACTIVE | (3U << 27) | (1U << 29) | 0x7FF;
        transfers[i].token = pid | ((unsigned int)address << 8) | ((unsigned int)endpoint << 15) |
                             ((unsigned int)((*toggle + i) & 1) << 19) | (((packet - 1) & 0x7FF) << 21);
        transfers[i].buffer = (uint32_t)(dma + offset);
        offset += packet; remaining -= packet;
    }
    __asm__ volatile("" ::: "memory");
    queue.element = (uint32_t)transfers;
    unsigned int completed = 0, bytes = 0;
    int result = -1;
    for (unsigned int ms = 0; ms < 200; ms++)
    {
        if (!connected() || (in16(io_base + 2) & 0x38)) break;
        while (completed < count)
        {
            unsigned int status = transfers[completed].status;
            if (status & ACTIVE) break;
            if (status & ERRORS) goto done;
            unsigned int actual = (status + 1) & 0x7FF;
            unsigned int requested = ((transfers[completed].token >> 21) + 1) & 0x7FF;
            if (actual > requested) goto done;
            bytes += actual; completed++;
            if (actual < requested || completed == count) { result = bytes; goto done; }
        }
        if (delay_ms(1)) break;
    }
done:
    queue.element = 1;
    __asm__ volatile("" ::: "memory");
    if (delay_ms(2)) result = -1;
    if (result >= 0)
    {
        *toggle = (*toggle + completed) & 1;
        if (pid == PID_IN && bytes) memcpy(buffer, dma, bytes);
    }
    return result;
}
static int control(unsigned char type, unsigned char request, unsigned short value,
                   unsigned short index, void *buffer, unsigned short length)
{
    unsigned char setup[8] = {type, request, value, value >> 8, index, index >> 8,
                              length, length >> 8};
    unsigned char toggle = 0;
    if (transfer(PID_SETUP, 0, ep0_size, &toggle, setup, 8) != 8) return -1;
    int result = 0;
    toggle = 1;
    if (length)
    {
        result = transfer(type & 0x80 ? PID_IN : PID_OUT, 0, ep0_size, &toggle, buffer, length);
        if (result < 0) return -1;
    }
    toggle = 1;
    if (transfer(type & 0x80 ? PID_OUT : PID_IN, 0, ep0_size, &toggle, NULL, 0)) return -1;
    return result;
}
static unsigned int little32(const unsigned char *p)
{ return p[0] | ((unsigned int)p[1] << 8) | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24); }
static unsigned int big32(const unsigned char *p)
{ return p[3] | ((unsigned int)p[2] << 8) | ((unsigned int)p[1] << 16) | ((unsigned int)p[0] << 24); }
static void store32(unsigned char *p, unsigned int n)
{ p[0] = n; p[1] = n >> 8; p[2] = n >> 16; p[3] = n >> 24; }

/* USB Mass Storage Bulk-Only Transport, transparent SCSI, LUN 0. */
static int command(const unsigned char *cdb, unsigned int cdb_length, void *data,
                   unsigned int size, int input)
{
    unsigned char cbw[31], csw[13];
    memset(cbw, 0, sizeof(cbw));
    store32(cbw, 0x43425355); store32(cbw + 4, ++tag); store32(cbw + 8, size);
    cbw[12] = input ? 0x80 : 0; cbw[14] = cdb_length;
    memcpy(cbw + 15, cdb, cdb_length);
    if (transfer(PID_OUT, bulk_out, out_size, &out_toggle, cbw, sizeof(cbw)) != sizeof(cbw)) goto error;
    if (size && transfer(input ? PID_IN : PID_OUT, input ? bulk_in : bulk_out,
                         input ? in_size : out_size, input ? &in_toggle : &out_toggle,
                         data, size) != (int)size) goto error;
    if (transfer(PID_IN, bulk_in, in_size, &in_toggle, csw, sizeof(csw)) != sizeof(csw) ||
        little32(csw) != 0x53425355 || little32(csw + 4) != tag ||
        little32(csw + 8) > size || csw[12] > 1) goto error;
    if (!csw[12] && little32(csw + 8)) goto error;
    return csw[12];
error:
    ready = 0; /* Next mount resets/re-enumerates the port, including BOT state. */
    return -1;
}
static int enumerate_device(void);
static int uhci_mouse(int n);
static unsigned int tsc_ms(void)
{
    unsigned int lo, hi;
    if (!tsc_per_ms) {
        __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
        unsigned int start = lo;
        delay_ms(10);
        __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
        tsc_per_ms = (lo - start) / 10 >> 10;   /* units of 1024 cycles */
        if (!tsc_per_ms) tsc_per_ms = 1;
    }
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return (hi << 22 | lo >> 10) / tsc_per_ms;   /* wraps after ~1000 s: only a rescan */
}
static void close_pipe(UsbDevice *d)
{
    if (d->d_pipe >= 0) usb_legacy_pipe_close(d->d_pipe);
    d->d_pipe = -1;
}
static int report(int n, unsigned int length);
/* A failed interrupt transfer does not end the mouse: receivers and hubs
 * see occasional errors. The next poll retries; every 8th failure in a row
 * also clears a halt on the device and restarts the data toggle; after 64
 * in a row the mouse is given up (the device stays listed). */
static int mouse_error(int n, unsigned int status)
{
    UsbDevice *d = &devices[n];
    d->d_errors++; d->d_last_error = status;
    if (++d->d_error_run > 64) {
        close_pipe(d);
        d->d_mouse = 0; d->d_pending = 0;
        return mouse_source(n + 1, 0, 0, 0, 0, 0, 0);
    }
    if (!(d->d_error_run & 7)) {
        control(0x02, 1, 0, 0x80 | d->d_mouse_ep, NULL, 0);   /* CLEAR_FEATURE(ENDPOINT_HALT) */
        d->d_mouse_toggle = 0;
    }
    return 0;
}
/* EHCI/OHCI: one poll of the mouse pipe of devices[n] (the current device). */
static int legacy_mouse(int n, unsigned int where)
{
    UsbDevice *d = &devices[n];
    int length = usb_legacy_mouse(d->d_pipe, address, d->d_mouse_ep, d->d_mouse_size, &d->d_mouse_toggle, d->d_report, where);
    if (length == -3) return mouse_error(n, usb_legacy_pipe_status(d->d_pipe));
    if (length == -1) {
        close_pipe(d);
        d->d_mouse = 0;
        return mouse_source(n + 1, 0, 0, 0, 0, 0, 0);
    }
    if (length < 0) return 0;
    d->d_reports++; d->d_error_run = 0;
    return length ? report(n, (unsigned int)length) : 0;
}
static void detach(int n)
{
    UsbDevice *d = &devices[n];
    if (d->d_mouse) mouse_source(n + 1, 0, 0, 0, 0, 0, 0);
    close_pipe(d);
    d->d_mouse_queue.element = 1;
    __asm__ volatile("" ::: "memory");
    d->d_mouse = d->d_pending = d->d_ready = 0;
    d->d_hub_port = 0;
    d->d_parent = -2;
}
static void detach_children(int hub)
{
    for (int n = root_count; n < device_count; n++)
        if (devices[n].d_parent == hub) detach(n);
}
/* UHCI walks one queue chain per controller: append the child's queue. */
static void link_queue(UsbDevice *d)
{
    if (d->d_legacy >= 0 || d->d_linked) return;
    QH *q = &queue;
    for (int i = 0; i < 64 && !(q->link & 1); i++) q = (QH *)(q->link & ~15U);
    d->d_mouse_queue.link = 1; d->d_mouse_queue.element = 1;
    __asm__ volatile("" ::: "memory");
    q->link = (uint32_t)&d->d_mouse_queue | 2;
    d->d_linked = (unsigned char)(d->d_controller + 1);
}
/* Look at every hub port once; reset and enumerate new devices. */
static int scan_hub(int hub)
{
    UsbDevice *saved = device; UsbController *saved_controller = controller;
    int saved_unit = unit, changed = 0;
    UsbDevice *h = &devices[hub];
    controller = &controllers[h->d_controller];
    h->d_scan_at = tsc_ms();
    for (unsigned int p = 1; p <= h->d_hub_ports; p++) {
        device = h; unit = hub;
        unsigned char data[4];
        if (control(0xA3, 0, 0, p, data, 4) != 4) break;
        unsigned int status = data[0] | data[1] << 8, change = data[2] | data[3] << 8;
        int child = -1;
        for (int n = root_count; n < device_count; n++)
            if (devices[n].d_parent == hub && devices[n].d_hub_port == p) child = n;
        if (!(status & 1) || (change & 1)) {
            if (child >= 0) { if (devices[child].d_mouse) changed = 1; detach(child); }
            child = -1;
            h->d_hub_seen &= ~(1U << p);
        }
        for (unsigned int bit = 0; bit < 5; bit++)
            if (change & (1U << bit)) control(0x23, 1, 16 + bit, p, NULL, 0);
        if (!(status & 1) || child >= 0 || (h->d_hub_seen & (1U << p))) continue;
        h->d_hub_seen |= 1U << p;
        int slot = -1;
        for (int n = root_count; n < device_count && slot < 0; n++)
            if (devices[n].d_parent == -2 && (!devices[n].d_linked || devices[n].d_linked == h->d_controller + 1)) slot = n;
        if (slot < 0) continue;
        delay_ms(100);                                   /* debounce */
        if (control(0x23, 3, 4, p, NULL, 0)) continue;   /* PORT_RESET */
        int reset = 0;
        for (int i = 0; i < 50 && !reset; i++) {
            delay_ms(10);
            if (control(0xA3, 0, 0, p, data, 4) != 4) break;
            status = data[0] | data[1] << 8;
            reset = !(status & 16) && (status & 2);
        }
        control(0x23, 1, 20, p, NULL, 0);                /* C_PORT_RESET */
        if (!reset) continue;
        delay_ms(10);
        UsbDevice *c = &devices[slot];
        unsigned char linked = c->d_linked;
        memset(c, 0, (unsigned int)((unsigned char *)&c->d_mouse_queue - (unsigned char *)c));
        c->d_linked = linked;
        c->d_parent = (signed char)hub; c->d_hub_port = (unsigned char)p;
        c->d_legacy = h->d_legacy; c->d_port = h->d_port; c->d_controller = h->d_controller;
        c->d_speed = status & 0x200 ? USB_SPEED_LOW : status & 0x400 ? USB_SPEED_HIGH : USB_SPEED_FULL;
        c->d_low = c->d_speed == USB_SPEED_LOW;
        link_queue(c);
        device = c; unit = slot;
        int result = enumerate_device();
        c->d_seen = 1;
        if (result == 1) changed = 1;
        if (result < 0) c->d_ready = 0;
    }
    device = saved; unit = saved_unit; controller = saved_controller;
    return changed;
}
/* Disk scans look for new devices behind hubs at once. */
static void rescan_hubs(void)
{
    for (int n = 0; n < root_count; n++)
        if (devices[n].d_hub && tsc_ms() - devices[n].d_scan_at >= 100) scan_hub(n);
}
/* A hub on a root port: power its ports and attach what is there. */
static int setup_hub(void)
{
    if (device->d_parent >= 0) return -1;                /* one level of hubs */
    unsigned char hub[16];
    if (control(0xA0, 6, 0x2900, 0, hub, 9) < 7) return -1;
    device->d_hub_ports = hub[2] > 15 ? 15 : hub[2];
    for (unsigned int p = 1; p <= device->d_hub_ports; p++) control(0x23, 3, 8, p, NULL, 0);
    delay_ms(hub[5] * 2U + 100);
    device->d_hub = 1; device->d_hub_seen = 0;
    scan_hub(unit);
    return 2;
}
/* Children are polled like root devices, through the hub's root port. */
static int poll_child(int n)
{
    UsbDevice *d = &devices[n];
    if (!d->d_hub_port || !d->d_mouse) return 0;
    if (d->d_legacy >= 0) return legacy_mouse(n, route());
    return uhci_mouse(n);   /* device is d: usb_mouse_poll selected it */
}
static int enumerate_port(unsigned short port)
{
    root_port = port;
    unsigned short status=device->d_legacy>=0?usb_legacy_status(device->d_legacy):in16(port);
    if(!(status&1))return -1;
    device->d_low=device->d_legacy>=0?!!(status&0x100):!!(status&(1U<<4));
    if(device->d_legacy>=0){if(usb_legacy_reset(device->d_legacy))return -1;}
    else{
        if(delay_ms(100))return -1;
        out16(port, (status & ~0x100F) | 0x200);
        if (delay_ms(50)) return -1;
        out16(port, in16(port) & ~0x200);
        if (delay_ms(10)) return -1;
        out16(port, (in16(port) & ~0x100A) | 0xE);
        if (delay_ms(10) || !connected()) return -1;
    }
    return enumerate_device();
}
/* Addresses and configures the device now at address 0 (its port was just
 * reset): 0 storage, 1 mouse, 2 hub, -1 nothing usable. */
static int enumerate_device(void)
{
    address = 0; ep0_size = 8; bulk_in = bulk_out = 0;
    for (int i = 0; i < 4; i++) device->d_classes[i] = 0;
    device->d_vid = device->d_pid = 0; device->d_hub = 0;
    in_toggle = out_toggle = 0;
    unsigned char descriptor[4096];
    if (control(0x80, 6, 0x100, 0, descriptor, 8) != 8) return -1;
    ep0_size = descriptor[7];
    if (ep0_size != 8 && ep0_size != 16 && ep0_size != 32 && ep0_size != 64) return -1;
    if (control(0, 5, unit + 1, 0, NULL, 0)) return -1;
    address = unit + 1;
    if (delay_ms(3)) return -1;
    if (control(0x80, 6, 0x100, 0, descriptor, 18) == 18) {
        device->d_vid = descriptor[8] | descriptor[9] << 8;
        device->d_pid = descriptor[10] | descriptor[11] << 8;
    }
    int hub_device = descriptor[4] == 9;
    if (control(0x80, 6, 0x200, 0, descriptor, 9) != 9 || descriptor[1] != 2) return -1;
    unsigned int total = descriptor[2] | ((unsigned int)descriptor[3] << 8);
    if (total < 9 || total > sizeof(descriptor) ||
        control(0x80, 6, 0x200, 0, descriptor, total) != (int)total) return -1;
    unsigned char configuration = descriptor[5];
    if(control(0,9,configuration,0,NULL,0))return -1;
    device->d_mouse_ep=0;
    for(unsigned int offset=9,k=0;offset+2<=total&&k<4;offset+=descriptor[offset]>=2?descriptor[offset]:total)
        if(descriptor[offset+1]==4&&descriptor[offset]>=9&&!descriptor[offset+3])device->d_classes[k++]=descriptor[offset+5]?descriptor[offset+5]:0xff;
    if(hub_device||(descriptor[9+1]==4&&descriptor[9+5]==9))return setup_hub();
    /* Composite receivers often expose a keyboard before the mouse. Inspect
     * each interface independently, including its own HID report descriptor. */
    for(unsigned int offset=9;offset+2<=total;){
        unsigned char *d=descriptor+offset;
        if(d[0]<2||d[0]>total-offset)return -1;
        if(d[1]!=4||d[0]<9||d[3]){offset+=d[0];continue;}
        unsigned int end=offset+d[0],report_length=0,endpoint=0,packet=0;
        int hid=d[5]==3,boot=hid&&d[6]==1&&d[7]==2;
        int mass=d[5]==8&&d[6]==6&&d[7]==0x50;
        bulk_in=bulk_out=0;
        while(end+2<=total&&descriptor[end+1]!=4){
            unsigned char *e=descriptor+end;
            if(e[0]<2||e[0]>total-end)return -1;
            if(hid&&e[1]==0x21&&e[0]>=9)
                for(unsigned int h=6;h+2<e[0];h+=3)
                    if(e[h]==0x22)report_length=e[h+1]|((unsigned int)e[h+2]<<8);
            if(e[1]==5&&e[0]>=7&&(e[2]&15)&&!(e[2]&0x70)){
                unsigned int size=e[4]|((unsigned int)e[5]<<8);
                if(mass&&(e[3]&3)==2&&size&&size<=(device->d_legacy>=0?512U:64U)){
                    if(e[2]&128){bulk_in=e[2]&15;in_size=size;}
                    else{bulk_out=e[2]&15;out_size=size;}
                }
                if(hid&&(e[3]&3)==3&&(e[2]&128)&&size&&size<=(device->d_low?8U:device->d_legacy>=0?1024U:64U)){
                    endpoint=e[2]&15;packet=size;
                }
            }
            end+=e[0];
        }
        interface_number=d[2];
        if(mass&&bulk_in&&bulk_out)break;
        if(hid&&endpoint){
            unsigned char report_descriptor[4096];
            int protocol=report_length&&report_length<=sizeof(report_descriptor)&&
                control(0x81,6,0x2200,interface_number,report_descriptor,report_length)==(int)report_length&&
                !hid_mouse_descriptor(&device->d_hid,report_descriptor,report_length);
            if(protocol&&boot&&control(0x21,11,1,interface_number,NULL,0))protocol=0;
            if(protocol||(boot&&packet>=3&&!control(0x21,11,0,interface_number,NULL,0))){
                close_pipe(device);
                if(device->d_legacy>=0&&(device->d_pipe=(signed char)usb_legacy_pipe(device->d_legacy))<0){offset=end;continue;}
                device->d_report_protocol=protocol;device->d_mouse_ep=endpoint;device->d_mouse_size=packet;
                device->d_reports=device->d_errors=device->d_last_error=0;device->d_error_run=0;
                control(0x21,10,0,interface_number,NULL,0);
                device->d_mouse=1;device->d_mouse_toggle=0;device->d_pending=0;
                mouse_source(unit+1,1,0,0,0,0,0);return 1;
            }
        }
        offset=end;
    }
    if(device->d_low)return -1;
    if (!bulk_in || !bulk_out) return -1;
    if (delay_ms(10)) return -1;
    /* Explicit BOT reset also establishes a known protocol state on remount. */
    if (control(0x21, 0xFF, 0, interface_number, NULL, 0)) return -1;
    unsigned char test[6] = {0}, sense[6] = {3, 0, 0, 0, 18, 0};
    int available = 0;
    for (int attempt = 0; attempt < 5; attempt++)
    {
        int result = command(test, sizeof(test), NULL, 0, 0);
        if (result == 0) { available = 1; break; }
        if (result < 0 || command(sense, sizeof(sense), descriptor, 18, 1) < 0) return -1;
        if (delay_ms(50)) return -1;
    }
    if (!available) return -1;
    unsigned char capacity_command[10] = {0x25};
    if (command(capacity_command, 10, descriptor, 8, 1) || big32(descriptor + 4) != 512 ||
        big32(descriptor) == 0xFFFFFFFFU) return -1;
    capacity = big32(descriptor) + 1;
    generation = ++generation_counter;   /* unique: hub children reuse entries */
    ready = 1;
    return 0;
}
int usb_storage_probe(void)
{
    if(xhci_unit>=0)return usb_xhci_storage_present(xhci_unit)?0:-1;
    if (usb_storage_present()) return 0;
    if (start_controller()) return -1;
    if (unit >= device_count) return -1;
    if (device->d_parent != -1) {           /* hub children are enumerated by their hub */
        rescan_hubs();
        return usb_storage_present() ? 0 : -1;
    }
    if((device->d_mouse||device->d_hub) && connected())return -1;
    int result=enumerate_port(root_port);
    device->d_seen=1;
    if(result==0)return 0;
    if(result>=1)return -1;
    if(device->d_legacy>=0)usb_legacy_disable(device->d_legacy);else out16(root_port, in16(root_port) & ~4U);
    ready = 0;
    return -1;
}
static int sectors_command(unsigned int lba, void *buffer, unsigned int count, int input)
{
    if(xhci_unit>=0)return usb_xhci_storage_transfer(xhci_unit,lba,buffer,count,input);
    if (!usb_storage_present() || !count || count > 8 || lba >= capacity || count > capacity - lba)
        return -1;
    unsigned char cdb[10] = {input ? 0x28 : 0x2A, 0, lba >> 24, lba >> 16, lba >> 8, lba,
                             0, count >> 8, count, 0};
    return command(cdb, sizeof(cdb), buffer, count * 512, input) == 0 ? 0 : -1;
}
int usb_storage_read(unsigned int lba, void *buffer, unsigned int count)
{ return sectors_command(lba, buffer, count, 1); }
int usb_storage_write(unsigned int lba, const void *buffer, unsigned int count)
{ return sectors_command(lba, (void *)buffer, count, 0); }
int usb_storage_flush(void)
{
    if(xhci_unit>=0)return usb_xhci_storage_flush(xhci_unit);
    if (!usb_storage_present()) return -1;
    unsigned char cdb[10] = {0x35};
    return command(cdb, sizeof(cdb), NULL, 0, 0) == 0 ? 0 : -1;
}

/* Persistent interrupt TDs share each controller schedule with bulk/control.
 * An idle mouse NAKs in hardware and never blocks keyboard polling. */
static int report(int n, unsigned int length)
{
    UsbDevice *d = &devices[n];
    if (d->d_report_protocol) return hid_mouse_report(&d->d_hid, n + 1, d->d_report, length);
    if (length >= 3) return mouse_source(n + 1, 1, (signed char)d->d_report[1], (signed char)d->d_report[2], d->d_report[0] & 7, 0, 0);
    return 0;
}
static int uhci_mouse(int n)
{
    int changed = 0;
    TD *td = &device->d_mouse_td;
    if (device->d_pending) {
        if (td->status & ACTIVE) return 0;
        device->d_mouse_queue.element = 1;
        if (td->status & ERRORS) {
            device->d_pending = 0;
            changed |= mouse_error(n, td->status >> 16 & 0xff);
            if (!device->d_mouse) return changed;
        } else {
            unsigned int length = (td->status + 1) & 0x7ff;
            device->d_mouse_toggle ^= 1;
            device->d_reports++; device->d_error_run = 0;
            if (length <= device->d_mouse_size) changed |= report(n, length);
        }
    }
    td->link = 1; td->buffer = (uint32_t)device->d_report;
    td->token = PID_IN | ((uint32_t)address << 8) | ((uint32_t)device->d_mouse_ep << 15) |
                ((uint32_t)device->d_mouse_toggle << 19) | ((device->d_mouse_size - 1U) << 21);
    td->status = ACTIVE | (3U << 27) | (1U << 29) | ((uint32_t)device->d_low << 26) | 0x7ff;
    __asm__ volatile("" ::: "memory");
    device->d_mouse_queue.element = (uint32_t)td;
    device->d_pending = 1;
    return changed;
}
int usb_mouse_poll(void)
{
    if(start_controller())return 0;
    mouse_poll_count++;
    UsbDevice *saved=device;UsbController *saved_controller=controller;int saved_unit=unit;
    int changed=0;
    for(int n=0;n<device_count;n++) {
        device=&devices[n];controller=&controllers[device->d_controller];unit=n;
        if(device->d_parent!=-1){
            if(device->d_parent>=0)changed|=poll_child(n);
            continue;
        }
        unsigned short state=device->d_legacy>=0?usb_legacy_status(device->d_legacy):in16(root_port);
        if(!(state&1) || (state&2)) {
            if(device->d_mouse)changed|=mouse_source(n+1,0,0,0,0,0,0);
            close_pipe(device);
            device->d_mouse_queue.element=1;
            __asm__ volatile("":::"memory");
            if(device->d_hub)detach_children(n);
            device->d_mouse=0;device->d_hub=0;device->d_seen=0;device->d_pending=0;device->d_enum_retries=0;device->d_retry_at=0;ready=0;
            if(state&2){if(device->d_legacy>=0)usb_legacy_ack(device->d_legacy);else out16(root_port,(state&~0xa)|2);}
        }
        if(!(state&1))continue;
        if(!device->d_seen && !ready &&
           (int)(mouse_poll_count-device->d_retry_at)>=0) {
            device->d_seen=1;
            int result=enumerate_port(root_port);
            if(result<0) {
                if(device->d_enum_retries<3) {
                    device->d_enum_retries++;
                    device->d_retry_at=mouse_poll_count+128;
                    device->d_seen=0;
                }
                continue;
            }
            if(result==2)changed=1;
            device->d_enum_retries=0;
        }
        /* Hubs report attach/detach on a status pipe; reading the ports
         * twice a second is cheaper than a second interrupt pipe. */
        if(device->d_hub){
            if(tsc_ms()-device->d_scan_at>=500)changed|=scan_hub(n);
            continue;
        }
        if(!device->d_mouse)continue;
        if(device->d_legacy>=0){changed|=legacy_mouse(n,USB_ROUTE_ROOT);continue;}
        changed|=uhci_mouse(n);
    }
    device=saved;controller=saved_controller;unit=saved_unit;
    return changed;
}

static void hex4(unsigned int value)
{
    char text[5];
    for (int i = 0; i < 4; i++) text[i] = "0123456789abcdef"[value >> (12 - 4 * i) & 15];
    text[4] = 0; print(WHITE, text);
}
static void describe(UsbDevice *d)
{
    hex4(d->d_vid); print(WHITE, ":"); hex4(d->d_pid);
    static const char *const speeds[] = {" full", " low", " high", ""};
    print(WHITE, speeds[d->d_parent >= 0 ? d->d_speed & 3 : 3]);
    if (d->d_hub) { print(WHITE, " hub, ports "); print_uint(WHITE, d->d_hub_ports); println(WHITE, ""); return; }
    if (d->d_mouse || d->d_reports || d->d_errors) {
        print(d->d_mouse ? GREEN : YELLOW, d->d_mouse ? " mouse" : " mouse (stopped)");
        print(WHITE, d->d_report_protocol ? " report" : " boot");
        print(WHITE, ", reports "); print_uint(WHITE, d->d_reports);
        print(d->d_errors ? YELLOW : WHITE, ", errors "); print_uint(d->d_errors ? YELLOW : WHITE, d->d_errors);
        if (d->d_errors) { print(YELLOW, " (status "); hex4(d->d_last_error); print(YELLOW, ")"); }
        println(WHITE, "");
        return;
    }
    if (d->d_ready) { println(GREEN, " storage"); return; }
    print(YELLOW, " not supported, interface classes");
    for (int i = 0; i < 4 && d->d_classes[i]; i++) { print(YELLOW, " "); print_uint(YELLOW, d->d_classes[i] == 0xff ? 0 : d->d_classes[i]); }
    println(YELLOW, d->d_classes[0] ? "" : " ? (no answer)");
}
/* devices lsblk: every controller and connected device, so a photo of the
 * screen tells what a real machine has. */
void usb_storage_diagnostics(void){
    start_controller();print(WHITE,"USB hosts (initialized/detected): UHCI ");print_uint(WHITE,controller_count);print(WHITE,"/");print_uint(WHITE,detected_count);
    usb_legacy_diagnostics();print(WHITE,", ");usb_xhci_diagnostics();println(WHITE,"");
    for(int n=0;n<root_count;n++){
        UsbDevice *d=&devices[n];
        unsigned int status=d->d_legacy>=0?usb_legacy_status(d->d_legacy):in16(d->d_port);
        if(!(status&1))continue;
        print(WHITE,"  ");
        print(WHITE,d->d_legacy<0?"UHCI":usb_legacy_is_ehci(d->d_legacy)?"EHCI":"OHCI");
        print(WHITE," port ");print_uint(WHITE,n);print(WHITE,": ");
        if(!d->d_seen){println(WHITE,"connected, not enumerated yet");continue;}
        describe(d);
        for(int c=root_count;c<device_count;c++)
            if(devices[c].d_parent==n){print(WHITE,"    hub port ");print_uint(WHITE,devices[c].d_hub_port);print(WHITE,": ");describe(&devices[c]);}
    }
    usb_xhci_details();
}
