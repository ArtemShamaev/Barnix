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
    unsigned char d_low, d_mouse_ep, d_mouse_toggle;
    unsigned short d_mouse_size;
    QH d_mouse_queue __attribute__((aligned(16)));
    TD d_mouse_td __attribute__((aligned(16)));
    unsigned char d_report[1024] __attribute__((aligned(16)));
} UsbDevice;
static UsbController controllers[USB_CONTROLLERS];
static UsbDevice devices[32];
static int initialized, controller_count, detected_count, device_count, unit;
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
    for(int n=0;n<32;n++)devices[n].d_legacy=-1;
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
    for(int n=0;n<legacy_count&&device_count<32;n++)devices[device_count++].d_legacy=n;
    controller = &controllers[0];
    return device_count ? 0 : -1;
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
    unit = index; device = &devices[index]; controller = &controllers[index < controller_count*2 ? index/2:0];
    return 0;
}
static int connected(void)
{
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

/* One synchronous packet chain, maximum 4096 bytes. Short IN packets stop
 * the chain; all descriptors are detached before their buffers can be reused. */
static int transfer(unsigned char pid, unsigned char endpoint, unsigned short max_packet,
                    unsigned char *toggle, void *buffer, unsigned int length)
{
    if(device->d_legacy>=0)return usb_legacy_transfer(device->d_legacy,address,endpoint,max_packet,pid,toggle,buffer,length);
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
static int enumerate_port(unsigned short port)
{
    root_port = port;
    unsigned short status=device->d_legacy>=0?usb_legacy_status(device->d_legacy):in16(port);
    if(!(status&1))return -1;
    device->d_low=!!(status&0x100);
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
    address = 0; ep0_size = 8; bulk_in = bulk_out = 0;
    in_toggle = out_toggle = 0;
    unsigned char descriptor[4096];
    if (control(0x80, 6, 0x100, 0, descriptor, 8) != 8) return -1;
    ep0_size = descriptor[7];
    if (ep0_size != 8 && ep0_size != 16 && ep0_size != 32 && ep0_size != 64) return -1;
    if (control(0, 5, unit + 1, 0, NULL, 0)) return -1;
    address = unit + 1;
    if (delay_ms(3)) return -1;
    if (control(0x80, 6, 0x200, 0, descriptor, 9) != 9 || descriptor[1] != 2) return -1;
    unsigned int total = descriptor[2] | ((unsigned int)descriptor[3] << 8);
    if (total < 9 || total > sizeof(descriptor) ||
        control(0x80, 6, 0x200, 0, descriptor, total) != (int)total) return -1;
    unsigned char configuration = descriptor[5];
    if(control(0,9,configuration,0,NULL,0))return -1;
    device->d_mouse_ep=0;
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
                device->d_report_protocol=protocol;device->d_mouse_ep=endpoint;device->d_mouse_size=packet;
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
    generation++;
    ready = 1;
    return 0;
}
int usb_storage_probe(void)
{
    if(xhci_unit>=0)return usb_xhci_storage_present(xhci_unit)?0:-1;
    if (usb_storage_present()) return 0;
    if(device->d_mouse && connected())return -1;
    if (start_controller()) return -1;
    if (unit >= device_count) return -1;
    int result=enumerate_port(root_port);
    device->d_seen=1;
    if(result==0)return 0;
    if(result==1)return -1;
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
int usb_mouse_poll(void)
{
    if(start_controller())return 0;
    UsbDevice *saved=device;UsbController *saved_controller=controller;int saved_unit=unit;
    int changed=0;
    for(int n=0;n<device_count;n++) {
        device=&devices[n];controller=&controllers[n<controller_count*2?n/2:0];unit=n;
        unsigned short state=device->d_legacy>=0?usb_legacy_status(device->d_legacy):in16(root_port);
        if(!(state&1) || (state&2)) {
            if(device->d_mouse)changed|=mouse_source(n+1,0,0,0,0,0,0);
            if(device->d_legacy>=0)usb_legacy_mouse_stop(device->d_legacy);
            device->d_mouse_queue.element=1;
            __asm__ volatile("":::"memory");
            device->d_mouse=0;device->d_seen=0;device->d_pending=0;ready=0;
            if(state&2){if(device->d_legacy>=0)usb_legacy_ack(device->d_legacy);else out16(root_port,(state&~0xa)|2);}
        }
        if(!(state&1))continue;
        if(!device->d_seen && !ready) {
            device->d_seen=1;
            if(enumerate_port(root_port)<0)continue;
        }
        if(!device->d_mouse)continue;
        if(device->d_legacy>=0){
            int length=usb_legacy_mouse(device->d_legacy,address,device->d_mouse_ep,device->d_mouse_size,&device->d_mouse_toggle,device->d_report);
            if(length==-1){usb_legacy_mouse_stop(device->d_legacy);device->d_mouse=0;changed|=mouse_source(n+1,0,0,0,0,0,0);}
            else if(length>0){
                if(device->d_report_protocol)changed|=hid_mouse_report(&device->d_hid,n+1,device->d_report,length);
                else if(length>=3)changed|=mouse_source(n+1,1,(signed char)device->d_report[1],(signed char)device->d_report[2],device->d_report[0]&7,0,0);
            }
            continue;
        }
        TD *td=&device->d_mouse_td;
        if(device->d_pending) {
            if(td->status&ACTIVE)continue;
            device->d_mouse_queue.element=1;
            if(td->status&ERRORS) {
                changed|=mouse_source(n+1,0,0,0,0,0,0);
                device->d_mouse=0;device->d_pending=0;
                continue;
            }
            unsigned int length=(td->status+1)&0x7ff;
            device->d_mouse_toggle^=1;
            if(length<=device->d_mouse_size)
                {
                    if(device->d_report_protocol)changed|=hid_mouse_report(&device->d_hid,n+1,device->d_report,length);
                    else if(length>=3)changed|=mouse_source(n+1,1,(signed char)device->d_report[1],(signed char)device->d_report[2],device->d_report[0]&7,0,0);
                }
        }
        td->link=1;td->buffer=(uint32_t)device->d_report;
        td->token=PID_IN|((uint32_t)address<<8)|((uint32_t)device->d_mouse_ep<<15)|
            ((uint32_t)device->d_mouse_toggle<<19)|((device->d_mouse_size-1U)<<21);
        td->status=ACTIVE|(3U<<27)|(1U<<29)|((uint32_t)device->d_low<<26)|0x7ff;
        __asm__ volatile("":::"memory");
        device->d_mouse_queue.element=(uint32_t)td;
        device->d_pending=1;
    }
    device=saved;controller=saved_controller;unit=saved_unit;
    return changed;
}

void usb_storage_diagnostics(void){
    start_controller();print(WHITE,"USB hosts (initialized/detected): UHCI ");print_uint(WHITE,controller_count);print(WHITE,"/");print_uint(WHITE,detected_count);
    usb_legacy_diagnostics();print(WHITE,", ");usb_xhci_diagnostics();println(WHITE,"");
    for(int n=0;n<device_count;n++){
        UsbDevice *d=&devices[n];
        unsigned int status=d->d_legacy>=0?usb_legacy_status(d->d_legacy):in16(d->d_port);
        if((status&1)&&d->d_seen&&!d->d_ready&&!d->d_mouse){
            print(WHITE,"USB port ");print_uint(WHITE,n);println(WHITE,": connected, no supported storage/mouse interface");
        }
    }
}
