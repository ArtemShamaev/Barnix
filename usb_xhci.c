#include "usb_xhci.h"
#include "barnix.h"
#include "linux_memory.h"
#include "mouse.h"
#include "storage_pci.h"
#include "usb_hid.h"
#include "usb_time.h"
#include <stdint.h>
/* Polling xHCI HID and Bulk-Only Mass Storage host. DMA uses identity-mapped, statically allocated memory.
 * All transfers have finite waits; an idle interrupt endpoint never blocks. */
#define HC_COUNT 2
#define SLOTS 16
#define RING_SIZE 64
#define EVENTS 256
#define TRB_TYPE(n) ((unsigned int)(n) << 10)
typedef struct {
  volatile uint32_t a, b, c, d;
} Trb;
typedef struct {
  Trb trbs[RING_SIZE];
  unsigned int next, cycle;
} __attribute__((aligned(1024))) Ring;
typedef struct {
  unsigned char input[33 * 64] __attribute__((aligned(64)));
  unsigned char output[32 * 64] __attribute__((aligned(64)));
  Ring control __attribute__((aligned(64)));
  Ring interrupt __attribute__((aligned(64)));
  Ring bulk_in __attribute__((aligned(64)));
  Ring bulk_out __attribute__((aligned(64)));
  unsigned char report[1024] __attribute__((aligned(4096)));
  HidMouse hid;
  unsigned int port, parent, parent_port, route, depth, speed, dci, packet,
      hub_ports, hub_protocol, hub_seen;
  unsigned int bulk_in_dci, bulk_out_dci, bulk_in_packet, bulk_out_packet;
  int active, mouse, boot_mouse, storage, pending, done, result;
  unsigned int bytes, control_bytes, control_length, control_status;
  unsigned int bulk_wait, bulk_length, bulk_bytes;
  int bulk_done, bulk_result;
} Device;
typedef struct {
  uint64_t dcbaa[SLOTS + 1] __attribute__((aligned(64)));
  uint64_t scratch_addresses[32] __attribute__((aligned(64)));
  unsigned char scratch[32][4096] __attribute__((aligned(4096)));
  Ring commands __attribute__((aligned(64)));
  Trb events[EVENTS] __attribute__((aligned(4096)));
  uint32_t erst[4] __attribute__((aligned(64)));
  Device devices[SLOTS + 1];
  volatile uint32_t *op, *doorbell, *intr;
  unsigned int context_size, ports, event_index, event_cycle, index,
      poll_counter;
  unsigned int seen_ports, superspeed_ports;
  uint32_t waiting;
  int completion, completion_slot, ready;
} Host;
static Host hosts[HC_COUNT];
static Host *host;
static int started, host_count, detected_count;
static unsigned char descriptor[4096] __attribute__((aligned(4096)));
static unsigned char control_dma[4096] __attribute__((aligned(4096)));
static unsigned char bulk_dma[4096] __attribute__((aligned(4096)));
typedef struct {
  unsigned int host, slot, sectors, generation, tag;
} Storage;
static Storage storage[16];
static unsigned int storage_generation;
static int setup_storage(unsigned int slot, unsigned int iface);
static int recover_endpoint(unsigned int slot, unsigned int dci, Ring *ring);

static uint32_t in32(uint16_t p) {
  uint32_t v;
  __asm__ volatile("inl %1,%0" : "=a"(v) : "Nd"(p));
  return v;
}
static void out32(uint16_t p, uint32_t v) {
  __asm__ volatile("outl %0,%1" ::"a"(v), "Nd"(p) : "memory");
}
static uint32_t pci(unsigned int bus, unsigned int slot, unsigned int fn,
                    unsigned int reg) {
  out32(0xcf8,
        0x80000000U | (bus << 16) | (slot << 11) | (fn << 8) | (reg & ~3U));
  return in32(0xcfc);
}
static void pci_enable(unsigned int bus, unsigned int slot, unsigned int fn) {
  unsigned short value = pci(bus, slot, fn, 4) | 6;
  out32(0xcf8, 0x80000004U | (bus << 16) | (slot << 11) | (fn << 8));
  __asm__ volatile("outw %0,%1" ::"a"(value), "Nd"((unsigned short)0xcfc));
}
static void delay(void) { usb_delay_ms(1); }
static void barrier(void) { __asm__ volatile("" ::: "memory"); }
static void write64(volatile uint32_t *p, unsigned int value) {
  p[0] = value;
  p[1] = 0;
}
static uint32_t *input_slot(Device *d) {
  return (uint32_t *)(d->input + host->context_size);
}
static uint32_t *input_ep(Device *d, unsigned int dci) {
  return (uint32_t *)(d->input + (dci + 1) * host->context_size);
}
static void ring_init(Ring *r) {
  memset(r, 0, sizeof(*r));
  r->cycle = 1;
}
static uint32_t enqueue(Ring *r, uint32_t a, uint32_t b, uint32_t c,
                        uint32_t control) {
  if (r->next == RING_SIZE - 1) {
    Trb *link = &r->trbs[r->next];
    link->a = (uint32_t)r->trbs;
    link->b = link->c = 0;
    barrier();
    link->d = TRB_TYPE(6) | 2 | r->cycle;
    r->next = 0;
    r->cycle ^= 1;
  }
  Trb *t = &r->trbs[r->next++];
  t->a = a;
  t->b = b;
  t->c = c;
  barrier();
  t->d = control | r->cycle;
  return (uint32_t)t;
}
static void events(void) {
  for (unsigned int count = 0; count < EVENTS; count++) {
    Trb *e = &host->events[host->event_index];
    uint32_t d = e->d;
    if ((d & 1) != host->event_cycle)
      break;
    barrier();
    unsigned int type = (d >> 10) & 63, code = e->c >> 24, slot = d >> 24,
                 endpoint = (d >> 16) & 31;
    if (type == 33 && e->a == host->waiting) {
      host->completion = code;
      host->completion_slot = slot;
    }
    if (type == 32 && slot > 0 && slot <= SLOTS) {
      Device *v = &host->devices[slot];
      if (endpoint == 1) {
        if (code == 13 && (e->c & 0xffffff) <= v->control_length)
          v->control_bytes = v->control_length - (e->c & 0xffffff);
        if (code != 1 && code != 13) {
          v->result = -1;
          v->done = 1;
        } else if (e->a == v->control_status) {
          v->result = 0;
          v->done = 1;
        }
      } else if (endpoint == v->bulk_wait) {
        v->bulk_result = (code == 1 || code == 13) ? 0 : -1;
        unsigned int residue = e->c & 0xffffff;
        if (residue > v->bulk_length)
          v->bulk_result = -1;
        else
          v->bulk_bytes = v->bulk_length - residue;
        v->bulk_done = 1;
      } else if (endpoint == v->dci && v->pending) {
        v->pending = 0;
        if ((code == 1 || code == 13) && (e->c & 0xffffff) <= v->packet) {
          v->bytes = v->packet - (e->c & 0xffffff);
          v->result = 0;
        } else {
          v->bytes = 0;
          v->result = -1;
        }
      }
    }
    host->event_index++;
    if (host->event_index == EVENTS) {
      host->event_index = 0;
      host->event_cycle ^= 1;
    }
    write64(host->intr + 6, (uint32_t)&host->events[host->event_index] | 8);
  }
}
static void stop_host(void) {
  /* A timed out command may still own DMA. Stop the controller and never
   * reuse this host's buffers or slots until the next system boot. */
  host->ready = 0;
  host->op[0] &= ~1U;
  for (int i = 0; i < 1000 && !(host->op[1] & 1); i++)
    delay();
  for (unsigned int n = 1; n <= SLOTS; n++)
    mouse_source(48 + host->index * SLOTS + n - 1, 0, 0, 0, 0, 0, 0);
}
static int command(unsigned int type, uint32_t pointer, unsigned int slot) {
  if (!host->ready)
    return -1;
  host->completion = 0;
  host->waiting =
      enqueue(&host->commands, pointer, 0, 0,
              TRB_TYPE(type) | ((slot & 255) << 24) | ((slot >> 8) << 16));
  barrier();
  host->doorbell[0] = 0;
  for (int i = 0; i < 500; i++) {
    events();
    if (host->completion)
      return host->completion == 1 ? host->completion_slot : -1;
    delay();
  }
  stop_host();
  return -1;
}
static int control(unsigned int slot, unsigned int type, unsigned int request,
                   unsigned int value, unsigned int index, void *data,
                   unsigned int length) {
  Device *v = &host->devices[slot];
  if (length > 4096 || !host->ready)
    return -1;
  if (length && !(type & 128))
    memcpy(control_dma, data, length);
  v->control_length = v->control_bytes = length;
  uint32_t a = type | (request << 8) | (value << 16),
           b = index | (length << 16);
  unsigned int trt = length ? (type & 128 ? 3 : 2) : 0;
  enqueue(&v->control, a, b, 8, TRB_TYPE(2) | (1U << 6) | (trt << 16));
  if (length)
    enqueue(&v->control, (uint32_t)control_dma, 0, length,
            TRB_TYPE(3) | (1U << 2) | ((type & 128) ? 1U << 16 : 0));
  v->control_status = enqueue(&v->control, 0, 0, 0,
                              TRB_TYPE(4) | (1U << 5) |
                                  ((!length || !(type & 128)) ? 1U << 16 : 0));
  v->done = 0;
  v->result = -1;
  barrier();
  host->doorbell[slot] = 1;
  for (int i = 0; i < 500; i++) {
    events();
    if (v->done) {
      if (v->result) {
        recover_endpoint(slot, 1, &v->control);
        return -1;
      }
      if (length && (type & 128))
        memcpy(data, control_dma, v->control_bytes);
      return v->control_bytes == length ? 0 : -1;
    }
    delay();
  }
  /* Stop before recycling a ring whose transfer has timed out. */
  recover_endpoint(slot, 1, &v->control);
  return -1;
}
static int recover_endpoint(unsigned int slot, unsigned int dci, Ring *ring) {
  Device *v = &host->devices[slot];
  unsigned int state =
      ((uint32_t *)(v->output + dci * host->context_size))[0] & 7;
  if (state == 1 && command(15, 0, slot | (dci << 8)) < 0) {
    stop_host();
    return -1;
  }
  if (state == 2 && command(14, 0, slot | (dci << 8)) < 0) {
    stop_host();
    return -1;
  }
  if (!host->ready)
    return -1;
  ring_init(ring);
  if (command(16, (uint32_t)ring->trbs | 1, slot | (dci << 8)) < 0) {
    stop_host();
    return -1;
  }
  return 0;
}
static void disconnect(unsigned int slot) {
  Device *v = &host->devices[slot];
  if (!v->active)
    return;
  for (unsigned int n = 1; n <= SLOTS; n++)
    if (host->devices[n].active && host->devices[n].parent == slot)
      disconnect(n);
  mouse_source(48 + host->index * SLOTS + slot - 1, 0, 0, 0, 0, 0, 0);
  if (v->parent)
    host->devices[v->parent].hub_seen &= ~(1U << (v->parent_port - 1));
  for (unsigned int i = 0; i < 16; i++)
    if (storage[i].slot == slot && storage[i].host == host->index)
      storage[i].slot = 0;
  if (command(10, 0, slot) < 0)
    return;
  v->active = 0;
  v->mouse = 0;
  v->pending = 0;
  host->seen_ports &= ~(1U << (v->port - 1));
  host->dcbaa[slot] = 0;
}
static unsigned int port_status(unsigned int port) {
  return host->op[0x100 + (port - 1) * 4];
}
static void port_write(unsigned int port, unsigned int bits) {
  unsigned int value = port_status(port);
  value &= ~((0x7fU << 17) | (1U << 1) | (1U << 4) | (1U << 31));
  host->op[0x100 + (port - 1) * 4] = value | bits;
}
static int reset_port(unsigned int port) {
  port_write(port, 1U << 9);
  usb_delay_ms(20);
  unsigned int state = port_status(port);
  if (!(state & 1))
    return -1;
  usb_delay_ms(100); /* USB connection debounce before reset/addressing. */
  /* HCRST does not reset attached USB devices. Firmware may have left an
   * enabled port with a nonzero device address: always reset before addressing.
   * A disabled SuperSpeed link needs warm reset; enabled links use hot reset.
   */
  unsigned int reset =
      (host->superspeed_ports & (1U << (port - 1))) && !(state & 2) ? 1U << 31
                                                                    : 1U << 4;
  port_write(port, reset);
  for (int i = 0; i < 1000; i++) {
    delay();
    state = port_status(port);
    if (!(state & reset) && (state & 2))
      break;
  }
  state = port_status(port);
  if ((state & 3) != 3)
    return -1;
  port_write(port, state & (0x7fU << 17));
  usb_delay_ms(10);
  return (state >> 10) & 15;
}
static void slot_context(Device *v, unsigned int entries) {
  uint32_t *s = input_slot(v);
  s[0] = v->route | (v->speed << 20) | (entries << 27);
  if (v->hub_ports)
    s[0] |= (1U << 26) | (v->hub_protocol == 2 ? 1U << 25 : 0);
  s[1] = (v->port << 16) | (v->hub_ports << 24);
  if (v->parent) {
    Device *p = &host->devices[v->parent];
    if ((v->speed == 1 || v->speed == 2) && p->speed == 3)
      s[2] = v->parent | (v->parent_port << 8);
    else
      s[2] = ((uint32_t *)p->output)[2] & 0xffff;
  }
}
static int setup_mouse(unsigned int slot, unsigned int iface,
                       unsigned int endpoint, unsigned int packet,
                       unsigned int interval, unsigned int report_length,
                       int boot) {
  Device *v = &host->devices[slot];
  if (!packet || packet > sizeof(v->report) || !(endpoint & 15))
    return -1;
  int parsed =
      report_length && report_length <= sizeof(descriptor) &&
      !control(slot, 0x81, 6, 0x2200, iface, descriptor, report_length) &&
      !hid_mouse_descriptor(&v->hid, descriptor, report_length);
  if (parsed && boot && control(slot, 0x21, 11, 1, iface, 0, 0))
    parsed = 0;
  if (!parsed &&
      (!boot || packet < 3 || control(slot, 0x21, 11, 0, iface, 0, 0)))
    return -1;
  v->boot_mouse = !parsed;
  /* SET_IDLE is optional for report-protocol HID interfaces. */
  control(slot, 0x21, 10, 0, iface, 0, 0);
  v->dci = (endpoint & 15) * 2 + 1;
  v->packet = packet;
  memset(v->input, 0, sizeof(v->input));
  ((uint32_t *)v->input)[1] = 1 | (1U << v->dci);
  slot_context(v, v->dci);
  uint32_t *ep = input_ep(v, v->dci);
  unsigned int exponent = 0;
  if (v->speed == 1 || v->speed == 2) {
    while ((1U << exponent) < interval && exponent < 7)
      exponent++;
    exponent += 3;
  } else
    exponent = interval ? interval - 1 : 0;
  ep[0] = (exponent > 15 ? 15 : exponent) << 16;
  ep[1] = (3U << 1) | (7U << 3) | (packet << 16);
  ring_init(&v->interrupt);
  ep[2] = (uint32_t)v->interrupt.trbs | 1;
  ep[4] = packet | (packet << 16);
  if (command(12, (uint32_t)v->input, slot) < 0)
    return -1;
  v->mouse = 1;
  v->pending = 0;
  v->bytes = 0;
  v->result = 0;
  mouse_source(48 + host->index * SLOTS + slot - 1, 1, 0, 0, 0, 0, 0);
  return 0;
}
static int configure(unsigned int slot) {
  Device *v = &host->devices[slot];
  if (control(slot, 0x80, 6, 0x100, 0, descriptor, 8))
    return -1;
  unsigned int packet = descriptor[7];
  if (v->speed >= 4) {
    if (packet != 9)
      return -1;
    packet = 512;
  }
  if (packet != 8 && packet != 16 && packet != 32 && packet != 64 &&
      packet != 512)
    return -1;
  memset(v->input, 0, sizeof(v->input));
  ((uint32_t *)v->input)[1] = 2;
  memcpy(input_ep(v, 1), v->output + host->context_size, host->context_size);
  input_ep(v, 1)[1] = (input_ep(v, 1)[1] & 0xffff) | (packet << 16);
  if (command(13, (uint32_t)v->input, slot) < 0)
    return -1;
  if (control(slot, 0x80, 6, 0x100, 0, descriptor, 18))
    return -1;
  unsigned int device_protocol = descriptor[6];
  if (control(slot, 0x80, 6, 0x200, 0, descriptor, 9))
    return -1;
  unsigned int total = descriptor[2] | ((unsigned int)descriptor[3] << 8);
  if (total < 9 || total > 2048)
    return -1;
  unsigned char config[2048];
  if (control(slot, 0x80, 6, 0x200, 0, config, total))
    return -1;
  if (control(slot, 0, 9, config[5], 0, 0, 0))
    return -1;
  /* Select HID or Mass Storage by interface class.  Bulk endpoint metadata
   * is retained here; BOT transport is layered on top of these endpoints. */
  for (unsigned int offset = 9; offset + 2 <= total;) {
    unsigned char *d = config + offset;
    if (d[0] < 2 || d[0] > total - offset)
      return -1;
    if (d[1] == 4 && d[0] >= 9 && d[3] == 0) {
      unsigned int iface = d[2], klass = d[5], protocol = d[7],
                   end = offset + d[0];
      unsigned int endpoint = 0, size = 0, interval = 0, report_length = 0;
      while (end + 2 <= total && config[end + 1] != 4) {
        unsigned char *e = config + end;
        if (e[0] < 2 || e[0] > total - end)
          return -1;
        if (e[1] == 0x21 && e[0] >= 9)
          for (unsigned int h = 6; h + 2 < e[0]; h += 3)
            if (e[h] == 0x22)
              report_length = e[h + 1] | ((unsigned int)e[h + 2] << 8);
        if (e[1] == 5 && e[0] >= 7 && (e[2] & 128) && (e[3] & 3) == 3) {
          endpoint = e[2];
          size = e[4] | ((unsigned int)(e[5] & 7) << 8);
          interval = e[6];
        }
        end += e[0];
      }
      if (klass == 8 && d[6] == 6 && protocol == 0x50) {
        unsigned int in_dci = 0, out_dci = 0, in_packet = 0, out_packet = 0;
        for (unsigned int q = offset + d[0]; q < end;) {
          unsigned char *e = config + q;
          if (e[0] < 2 || e[0] > total - q)
            return -1;
          if (e[1] == 5 && e[0] >= 7 && (e[3] & 3) == 2) {
            unsigned int ep = e[2],
                         packet = e[4] | ((unsigned int)(e[5] & 7) << 8);
            if (ep & 0x80) {
              in_dci = (ep & 15) * 2 + 1;
              in_packet = packet;
            } else {
              out_dci = (ep & 15) * 2;
              out_packet = packet;
            }
          }
          q += e[0];
        }
        if (in_dci && out_dci && in_packet && out_packet) {
          v->storage = 1;
          v->bulk_in_dci = in_dci;
          v->bulk_out_dci = out_dci;
          v->bulk_in_packet = in_packet;
          v->bulk_out_packet = out_packet;
          return setup_storage(slot, iface);
        }
      }
      if (klass == 3 && endpoint && !(endpoint & 0x70) &&
          setup_mouse(slot, iface, endpoint, size, interval, report_length,
                      d[6] == 1 && protocol == 2) == 0)
        return 0;
      if (klass == 9) {
        if (control(slot, 0xa0, 6, (v->speed >= 4 ? 0x2a : 0x29) << 8, 0,
                    descriptor, 9))
          return -1;
        v->hub_ports = descriptor[2];
        v->hub_protocol = device_protocol;
        unsigned int power_delay = descriptor[5] * 2;
        if (!v->hub_ports || v->hub_ports > 15 || v->depth >= 5)
          return -1;
        memset(v->input, 0, sizeof(v->input));
        ((uint32_t *)v->input)[1] = 1;
        slot_context(v, 1);
        input_slot(v)[2] |= ((descriptor[3] >> 5) & 3) << 16;
        if (command(13, (uint32_t)v->input, slot) < 0)
          return -1;
        if (v->speed >= 4)
          control(slot, 0x20, 12, v->depth, 0, 0, 0);
        for (unsigned int p = 1; p <= v->hub_ports; p++)
          control(slot, 0x23, 3, 8, p, 0, 0);
        usb_delay_ms(power_delay + 100);
        return 0;
      }
      offset = end;
      continue;
    }
    offset += d[0];
  }
  return -1;
}
static int attach(unsigned int port, unsigned int parent,
                  unsigned int parent_port, unsigned int speed) {
  int slot = command(9, 0, 0);
  if (slot < 1 || slot > SLOTS)
    return -1;
  Device *v = &host->devices[slot];
  memset(v, 0, sizeof(*v));
  v->active = 1;
  v->port = port;
  v->speed = speed;
  v->parent = parent;
  v->parent_port = parent_port;
  if (parent) {
    Device *p = &host->devices[parent];
    v->depth = p->depth + 1;
    v->route = p->route | (parent_port << (p->depth * 4));
  }
  host->dcbaa[slot] = (uint32_t)v->output;
  ((uint32_t *)v->input)[1] = 3;
  slot_context(v, 1);
  ring_init(&v->control);
  uint32_t *ep = input_ep(v, 1);
  ep[1] = (3U << 1) | (4U << 3) |
          ((speed >= 4   ? 512U
            : speed == 3 ? 64U
                         : 8U)
           << 16);
  ep[2] = (uint32_t)v->control.trbs | 1;
  ep[4] = 8;
  if (command(11, (uint32_t)v->input, slot) < 0 || configure(slot)) {
    disconnect(slot);
    return -1;
  }
  return slot;
}
static unsigned int child_slot(unsigned int parent, unsigned int port) {
  for (unsigned int n = 1; n <= SLOTS; n++) {
    Device *v = &host->devices[n];
    if (v->active && v->parent == parent &&
        (parent ? v->parent_port : v->port) == port)
      return n;
  }
  return 0;
}
static void scan_hub(unsigned int slot) {
  Device *v = &host->devices[slot];
  for (unsigned int p = 1; p <= v->hub_ports; p++) {
    unsigned char data[4];
    if (control(slot, 0xa3, 0, 0, p, data, 4))
      return;
    unsigned int status = data[0] | ((unsigned int)data[1] << 8),
                 change = data[2] | ((unsigned int)data[3] << 8),
                 child = child_slot(slot, p);
    if (!(status & 1) || (change & 1)) {
      if (child)
        disconnect(child);
      child = 0;
      v->hub_seen &= ~(1U << (p - 1));
    }
    for (unsigned int bit = 0; bit < 5; bit++)
      if (change & (1U << bit))
        control(slot, 0x23, 1, 16 + bit, p, 0, 0);
    if (!(status & 1) || child || (v->hub_seen & (1U << (p - 1))))
      continue;
    v->hub_seen |= 1U << (p - 1);
    if (control(slot, 0x23, 3, 4, p, 0, 0))
      continue;
    int reset = 0;
    for (int i = 0; i < 100; i++) {
      delay();
      if (control(slot, 0xa3, 0, 0, p, data, 4))
        break;
      status = data[0] | ((unsigned int)data[1] << 8);
      if (!(status & 16) && (status & 2)) {
        reset = 1;
        break;
      }
    }
    if (!reset)
      continue;
    control(slot, 0x23, 1, 20, p, 0, 0);
    unsigned int speed = v->speed >= 4      ? 4
                         : (status & 0x400) ? 3
                         : (status & 0x200) ? 2
                                            : 1;
    usb_delay_ms(10);
    attach(v->port, slot, p, speed);
    v->hub_seen |= 1U << (p - 1);
  }
}
static int initialize(unsigned int base, unsigned int index) {
  if (base > 0xffff0000U || linux_memory_map_mmio(base, 65536))
    return -1;
  volatile uint32_t *cap = (volatile uint32_t *)base;
  unsigned int caplength = cap[0] & 255, hcs = cap[1], hcc = cap[4],
               rt = cap[6] & ~31U, db = cap[5] & ~3U;
  if (caplength < 32 || rt > 0xf000 || db > 0xf000)
    return -1;
  host = &hosts[index];
  memset(host, 0, sizeof(*host));
  host->index = index;
  host->context_size = (hcc & 4) ? 64 : 32;
  host->ports = hcs >> 24;
  if (host->ports > 32)
    host->ports = 32;
  host->op = (volatile uint32_t *)(base + caplength);
  host->doorbell = (volatile uint32_t *)(base + db);
  host->intr = (volatile uint32_t *)(base + rt + 0x20);
  unsigned int ext = hcc >> 16;
  for (unsigned int i = 0; ext && i < 64; i++) {
    if (ext > 16380)
      return -1;
    volatile uint32_t *e = cap + ext;
    unsigned int value = e[0];
    if ((value & 255) == 1) {
      e[0] = value | (1U << 24);
      for (int w = 0; w < 1000 && (e[0] & (1U << 16)); w++)
        delay();
      if (e[0] & (1U << 16))
        return -1;
      e[1] = 0;
    }
    if ((value & 255) == 2 && (value >> 24) >= 3) {
      unsigned int first = e[2] & 255, count = (e[2] >> 8) & 255;
      if (!first || count > 32 || first + count - 1 > host->ports)
        return -1;
      for (unsigned int p = first; p < first + count; p++)
        host->superspeed_ports |= 1U << (p - 1);
    }
    if (!((value >> 8) & 255))
      break;
    ext += (value >> 8) & 255;
  }
  host->op[0] &= ~1U;
  for (int i = 0; i < 500 && !(host->op[1] & 1); i++)
    delay();
  if (!(host->op[1] & 1))
    return -1;
  host->op[0] = 2;
  for (int i = 0; i < 1000 && ((host->op[0] & 2) || (host->op[1] & (1U << 11)));
       i++)
    delay();
  if ((host->op[0] & 2) || (host->op[1] & (1U << 11)) || !(host->op[2] & 1))
    return -1;
  unsigned int scratch = ((cap[2] >> 27) & 31) | ((cap[2] >> 16) & 0x3e0);
  if (scratch > 32)
    return -1;
  for (unsigned int i = 0; i < scratch; i++)
    host->scratch_addresses[i] = (uint32_t)host->scratch[i];
  if (scratch)
    host->dcbaa[0] = (uint32_t)host->scratch_addresses;
  ring_init(&host->commands);
  host->event_cycle = 1;
  host->erst[0] = (uint32_t)host->events;
  host->erst[2] = EVENTS;
  write64(host->op + 12, (uint32_t)host->dcbaa);
  write64(host->op + 6, (uint32_t)host->commands.trbs | 1);
  host->intr[0] = 0;
  host->intr[2] = 1;
  write64(host->intr + 4, (uint32_t)host->erst);
  write64(host->intr + 6, (uint32_t)host->events);
  host->op[14] = (hcs & 255) > SLOTS ? SLOTS : (hcs & 255);
  barrier();
  host->op[0] = 1;
  for (int i = 0; i < 100 && (host->op[1] & 1); i++)
    delay();
  if (host->op[1] & 1)
    return -1;
  host->ready = 1;
  return 0;
}
static void start(void) {
  if (started)
    return;
  started = 1;
  for (unsigned int b = 0; b < 256 && host_count < HC_COUNT; b++)
    for (unsigned int s = 0; s < 32 && host_count < HC_COUNT; s++) {
      if ((pci(b, s, 0, 0) & 0xffff) == 0xffff)
        continue;
      unsigned int functions = (pci(b, s, 0, 12) & 0x800000) ? 8 : 1;
      for (unsigned int f = 0; f < functions && host_count < HC_COUNT; f++)
        if ((pci(b, s, f, 8) >> 8) == 0x0c0330) {
          detected_count++;
          unsigned int bar =
              storage_pci_mmio_bar((b << 16) | (s << 11) | (f << 8), 0);
          if (!bar)
            continue;
          pci_enable(b, s, f);
          if (!initialize(bar & ~15U, host_count))
            host_count++;
        }
    }
}
int usb_xhci_controller_count(void) {
  start();
  return host_count;
}
int usb_xhci_poll(void) {
  start();
  BarnixMouse before;
  mouse_get(&before);
  for (int h = 0; h < host_count; h++) {
    host = &hosts[h];
    if (!host->ready)
      continue;
    events();
    for (unsigned int p = 1; p <= host->ports; p++) {
      unsigned int state = port_status(p), slot = child_slot(0, p);
      if (!(state & 1) || (state & (1U << 17))) {
        if (slot)
          disconnect(slot);
        slot = 0;
        host->seen_ports &= ~(1U << (p - 1));
      }
      if (state & (0x7fU << 17))
        port_write(p, state & (0x7fU << 17));
      if ((state & 1) && !slot && !(host->seen_ports & (1U << (p - 1)))) {
        host->seen_ports |= 1U << (p - 1);
        int speed = reset_port(p);
        if (speed > 0)
          attach(p, 0, 0, speed);
        host->seen_ports |= 1U << (p - 1);
      }
    }
    if (!(host->poll_counter++ & 127))
      for (unsigned int n = 1; n <= SLOTS; n++)
        if (host->devices[n].active && host->devices[n].hub_ports)
          scan_hub(n);
    for (unsigned int n = 1; n <= SLOTS; n++) {
      Device *v = &host->devices[n];
      if (!v->active || !v->mouse || v->pending)
        continue;
      if (v->result < 0) {
        disconnect(n);
        continue;
      }
      if (v->bytes) {
        if (!v->boot_mouse)
          hid_mouse_report(&v->hid, 48 + h * SLOTS + n - 1, v->report,
                           v->bytes);
        else if (v->bytes >= 3)
          mouse_source(48 + h * SLOTS + n - 1, 1, (signed char)v->report[1],
                       (signed char)v->report[2], v->report[0] & 7, 0, 0);
        v->bytes = 0;
      }
      enqueue(&v->interrupt, (uint32_t)v->report, 0, v->packet,
              TRB_TYPE(1) | (1U << 5) | (1U << 2));
      v->pending = 1;
      barrier();
      host->doorbell[n] = v->dci;
    }
  }
  BarnixMouse after;
  mouse_get(&after);
  return before.sequence != after.sequence;
}

/* Bulk transfers use a page-aligned bounce buffer: a Normal TRB must not
 * cross a 64 KiB boundary, and callers need not supply DMA-safe memory. */
static int bulk(unsigned int slot, int input, void *data, unsigned int length) {
  Device *v = &host->devices[slot];
  if (!host->ready || !v->active || length > sizeof(bulk_dma))
    return -1;
  Ring *r = input ? &v->bulk_in : &v->bulk_out;
  unsigned int dci = input ? v->bulk_in_dci : v->bulk_out_dci;
  if (!input && length)
    memcpy(bulk_dma, data, length);
  v->bulk_wait = dci;
  v->bulk_length = length;
  v->bulk_done = 0;
  v->bulk_result = -1;
  enqueue(r, (uint32_t)bulk_dma, 0, length,
          TRB_TYPE(1) | (1U << 5) | (1U << 2));
  barrier();
  host->doorbell[slot] = dci;
  for (int i = 0; i < 1000; i++) {
    events();
    if (v->bulk_done) {
      v->bulk_wait = 0;
      if (v->bulk_result)
        return -1;
      if (input && v->bulk_bytes)
        memcpy(data, bulk_dma, v->bulk_bytes);
      return v->bulk_bytes;
    }
    delay();
  }
  recover_endpoint(slot, dci, r);
  v->bulk_wait = 0;
  return -1;
}
static unsigned int le32(const unsigned char *p) {
  return p[0] | ((unsigned int)p[1] << 8) | ((unsigned int)p[2] << 16) |
         ((unsigned int)p[3] << 24);
}
static unsigned int be32(const unsigned char *p) {
  return p[3] | ((unsigned int)p[2] << 8) | ((unsigned int)p[1] << 16) |
         ((unsigned int)p[0] << 24);
}
static void put32(unsigned char *p, unsigned int v) {
  for (int i = 0; i < 4; i++)
    p[i] = v >> (i * 8);
}
static int scsi(Storage *s, const unsigned char *cdb, unsigned int cdb_length,
                void *data, unsigned int length, int input) {
  unsigned char cbw[31] = {0}, csw[13];
  put32(cbw, 0x43425355);
  put32(cbw + 4, ++s->tag);
  put32(cbw + 8, length);
  cbw[12] = input ? 128 : 0;
  cbw[14] = cdb_length;
  memcpy(cbw + 15, cdb, cdb_length);
  if (bulk(s->slot, 0, cbw, 31) != 31)
    return -1;
  int actual = length ? bulk(s->slot, input, data, length) : 0;
  if (actual < 0)
    return -1;
  if (bulk(s->slot, 1, csw, 13) != 13 || le32(csw) != 0x53425355 ||
      le32(csw + 4) != s->tag || csw[12] > 1 || le32(csw + 8) > length)
    return -1;
  if (!csw[12] && ((unsigned int)actual != length || le32(csw + 8)))
    return -1;
  return csw[12];
}
static int setup_storage(unsigned int slot, unsigned int iface) {
  Device *v = &host->devices[slot];
  unsigned int in = v->bulk_in_dci, out = v->bulk_out_dci;
  if (in > 31 || out > 30 || !in || !out || v->bulk_in_packet > 1024 ||
      v->bulk_out_packet > 1024)
    return -1;
  memset(v->input, 0, sizeof(v->input));
  ((uint32_t *)v->input)[1] = 1 | (1U << in) | (1U << out);
  slot_context(v, in > out ? in : out);
  ring_init(&v->bulk_in);
  ring_init(&v->bulk_out);
  uint32_t *ep = input_ep(v, in);
  ep[1] = (3U << 1) | (6U << 3) | (v->bulk_in_packet << 16);
  ep[2] = (uint32_t)v->bulk_in.trbs | 1;
  ep[4] = 512;
  ep = input_ep(v, out);
  ep[1] = (3U << 1) | (2U << 3) | (v->bulk_out_packet << 16);
  ep[2] = (uint32_t)v->bulk_out.trbs | 1;
  ep[4] = 512;
  if (command(12, (uint32_t)v->input, slot) < 0 ||
      control(slot, 0x21, 0xff, 0, iface, 0, 0))
    return -1;
  Storage next = {.host = host->index, .slot = slot};
  unsigned char test[6] = {0}, sense[6] = {3, 0, 0, 0, 18, 0}, response[18];
  int available = 0;
  for (int n = 0; n < 5; n++) {
    int result = scsi(&next, test, 6, 0, 0, 0);
    if (!result) {
      available = 1;
      break;
    }
    if (result < 0 || scsi(&next, sense, 6, response, 18, 1) < 0)
      return -1;
    for (int i = 0; i < 50; i++)
      delay();
  }
  unsigned char capacity[10] = {0x25};
  if (!available || scsi(&next, capacity, 10, response, 8, 1) ||
      be32(response + 4) != 512 || be32(response) == 0xffffffffU)
    return -1;
  next.sectors = be32(response) + 1;
  next.generation = ++storage_generation;
  for (unsigned int i = 0; i < 16; i++)
    if (!storage[i].slot) {
      storage[i] = next;
      return 0;
    }
  return -1;
}
static Storage *get_storage(int index) {
  if (index < 0 || index >= 16)
    return 0;
  usb_xhci_poll();
  Storage *s = &storage[index];
  if (!s->slot)
    return 0;
  host = &hosts[s->host];
  return host->ready ? s : 0;
}
int usb_xhci_storage_present(int index) { return get_storage(index) != 0; }
unsigned int usb_xhci_storage_sectors(int index) {
  Storage *s = get_storage(index);
  return s ? s->sectors : 0;
}
unsigned int usb_xhci_storage_generation(int index) {
  Storage *s = get_storage(index);
  return s ? s->generation : 0;
}
int usb_xhci_storage_transfer(int index, unsigned int lba, void *data,
                              unsigned int count, int input) {
  Storage *s = get_storage(index);
  if (!s || !data || !count || count > 8 || lba >= s->sectors ||
      count > s->sectors - lba)
    return -1;
  unsigned char cdb[10] = {
      input ? 0x28 : 0x2a, 0,     lba >> 24, lba >> 16, lba >> 8, lba, 0,
      count >> 8,          count, 0};
  int result = scsi(s, cdb, 10, data, count * 512, input);
  if (result < 0) {
    unsigned int slot = s->slot;
    disconnect(slot);
  }
  return result ? -1 : 0;
}
int usb_xhci_storage_flush(int index) {
  Storage *s = get_storage(index);
  if (!s)
    return -1;
  unsigned char cdb[10] = {0x35};
  int result = scsi(s, cdb, 10, 0, 0, 0);
  if (result < 0)
    disconnect(s->slot);
  return result ? -1 : 0;
}

void usb_xhci_diagnostics(void) {
  start();
  print(WHITE, "xHCI ");
  print_uint(WHITE, host_count);
  print(WHITE, "/");
  print_uint(WHITE, detected_count);
  unsigned int failed = 0;
  for (int h = 0; h < host_count; h++) {
    host = &hosts[h];
    if (!host->ready) {
      failed++;
      continue;
    }
    for (unsigned int p = 1; p <= host->ports; p++)
      if ((port_status(p) & 1) && !child_slot(0, p))
        failed++;
  }
  if (failed) {
    print(WHITE, " (unavailable ports/hosts: ");
    print_uint(WHITE, failed);
    print(WHITE, ")");
  }
}
