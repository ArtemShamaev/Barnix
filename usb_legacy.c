#include "usb_legacy.h"
#include "barnix.h"
#include "linux_memory.h"
#include "storage_pci.h"
#include "usb_time.h"
#include <stdint.h>
#define HOSTS 8
#define PORTS 16
/* EHCI qTD/QH are 32-byte aligned; OHCI ED/TD are 16-byte aligned. */
typedef struct {
  volatile uint32_t next, alt, token, buffer[5], high[5], padding[3];
} Qtd;
typedef struct {
  volatile uint32_t link, ep, cap, current;
  Qtd overlay;
  uint32_t pad[4];
} EhciQh;
typedef struct {
  volatile uint32_t flags, tail, head, next;
} Ed;
typedef struct {
  volatile uint32_t flags, buffer, next, end;
} Ot;
typedef struct {
  uint32_t frames[1024] __attribute__((aligned(4096)));
  unsigned char hcca[256] __attribute__((aligned(256)));
  EhciQh qh __attribute__((aligned(64)));
  Qtd qtd __attribute__((aligned(32)));
  Ed ed __attribute__((aligned(16)));
  Ot td[2] __attribute__((aligned(16)));
  unsigned char dma[4096] __attribute__((aligned(4096)));
  /* Never-executed heads of the interrupt lists; pipes hang behind them. */
  EhciQh anchor __attribute__((aligned(64)));
  Ed anchor_ed __attribute__((aligned(16)));
  volatile uint32_t *op;
  unsigned int kind, ports, first, live;
} Host;
typedef struct {
  unsigned int host, port;
} Port;
/* An interrupt IN pipe (a mouse), any number per root port so several
 * mice behind one hub all work. EHCI alternates two qTDs that point at
 * each other: the controller waits on the inactive one, and software arms it
 * while the other is being used, so the schedule never stops. OHCI appends
 * to the ED's TD list the usual way, through the dummy tail TD. */
#define PIPES 16
#define PIPE_PACKET 512
typedef struct {
  EhciQh qh __attribute__((aligned(64)));
  Qtd qtd[2] __attribute__((aligned(64)));
  Ed ed __attribute__((aligned(16)));
  Ot td[2] __attribute__((aligned(16)));
  unsigned char dma[2][PIPE_PACKET] __attribute__((aligned(4096)));
  int port, used, started, current;
  unsigned int status;
} Pipe;
static Pipe pipes[PIPES];
static Host hosts[HOSTS];
static Port ports[PORTS];
static int initialized, nhost, nport;
static unsigned int detected[2], running[2];
static uint32_t in32(unsigned short p) {
  uint32_t v;
  __asm__ volatile("inl %1,%0" : "=a"(v) : "Nd"(p));
  return v;
}
static void out32(unsigned short p, uint32_t v) {
  __asm__ volatile("outl %0,%1" ::"a"(v), "Nd"(p) : "memory");
}
static uint32_t pci(unsigned int b, unsigned int s, unsigned int f,
                    unsigned int r) {
  out32(0xcf8, 0x80000000U | (b << 16) | (s << 11) | (f << 8) | (r & ~3U));
  return in32(0xcfc);
}
static void pciset(unsigned int b, unsigned int s, unsigned int f,
                   unsigned int r, uint32_t v) {
  out32(0xcf8, 0x80000000U | (b << 16) | (s << 11) | (f << 8) | (r & ~3U));
  out32(0xcfc, v);
}
static void barrier(void) { __asm__ volatile("" ::: "memory"); }
static int wait_bits(volatile uint32_t *p, uint32_t mask, uint32_t value,
                     unsigned int ms) {
  for (unsigned int n = 0; n < ms; n++) {
    if ((*p & mask) == value)
      return 0;
    if (usb_delay_ms(1))
      break;
  }
  return -1;
}
static void stop(Host *h) {
  h->live = 0;
  if (h->kind == 0x20) {
    h->op[0] &= ~1U;
    wait_bits(h->op + 1, 1U << 12, 1U << 12, 100);
  } else {
    h->op[1] = 0;
    usb_delay_ms(2);
  }
}
static volatile uint32_t *port_reg(Port *p) {
  Host *h = &hosts[p->host];
  return h->op + (h->kind == 0x20 ? 0x11 : 0x15) + p->port;
}
static void ehci_port(volatile uint32_t *reg, unsigned int clear,
                      unsigned int set) {
  *reg = (*reg & ~(0x2a | clear)) | set;
}
static int async_schedule(Host *h, int on) {
  if (on)
    h->op[0] |= 1U << 5;
  else
    h->op[0] &= ~(1U << 5);
  if (wait_bits(h->op + 1, 1U << 15, on ? 1U << 15 : 0, 100)) {
    stop(h);
    return -1;
  }
  return 0;
}
static int periodic_schedule(Host *h, int on) {
  if (on)
    h->op[0] |= 1U << 4;
  else
    h->op[0] &= ~(1U << 4);
  if (wait_bits(h->op + 1, 1U << 14, on ? 1U << 14 : 0, 100)) {
    stop(h);
    return -1;
  }
  return 0;
}
static int initialize(unsigned int b, unsigned int s, unsigned int f,
                      unsigned int kind, unsigned int base) {
  if (linux_memory_map_mmio(base, 4096))
    return -1;
  Host *h = &hosts[nhost];
  memset(h, 0, sizeof(*h));
  h->kind = kind;
  h->first = nport;
  unsigned short command = pci(b, s, f, 4) | 6;
  out32(0xcf8, 0x80000004U | (b << 16) | (s << 11) | (f << 8));
  __asm__ volatile("outw %0,%1" ::"a"(command), "Nd"((unsigned short)0xcfc));
  volatile uint32_t *cap = (volatile uint32_t *)base;
  if (kind == 0x20) {
    unsigned int offset = (cap[2] >> 8) & 255;
    for (unsigned int n = 0; offset && n < 48; n++) {
      if (offset < 0x40 || offset > 0xf8 || (offset & 3))
        return -1;
      unsigned int v = pci(b, s, f, offset);
      if ((v & 255) == 1) {
        pciset(b, s, f, offset, v | (1U << 24));
        unsigned int t = 1000;
        while ((pci(b, s, f, offset) & (1U << 16)) && t--)
          usb_delay_ms(1);
        if (pci(b, s, f, offset) & (1U << 16))
          return -1;
        pciset(b, s, f, offset + 4, 0);
      }
      offset = (v >> 8) & 255;
    }
    unsigned int length = cap[0] & 255;
    if (length < 16 || length > 128)
      return -1;
    h->op = (volatile uint32_t *)(base + length);
    h->ports = cap[1] & 15;
    h->op[0] &= ~1U;
    if (wait_bits(h->op + 1, 1U << 12, 1U << 12, 100))
      return -1;
    h->op[0] = 2;
    if (wait_bits(h->op, 2, 0, 100))
      return -1;
    h->op[2] = 0;
    h->op[1] = 0x3f;
    h->op[4] = 0;
    h->op[3] = 0;
    h->qh.link = (uint32_t)&h->qh | 2;
    h->qh.ep = (1U << 15) | (1U << 14) | (2U << 12) | (64U << 16);
    h->qh.overlay.next = h->qh.overlay.alt = 1;
    h->qh.overlay.token = 1U << 6;
    h->op[6] = (uint32_t)&h->qh;
    for (unsigned int i = 0; i < 1024; i++)
      h->frames[i] = 1;
    h->op[5] = (uint32_t)h->frames;
    h->op[0] = 1 | (8U << 16);
    h->op[0x10] = 1;
    if (wait_bits(h->op + 1, 1U << 12, 0, 100))
      return -1;
    for (unsigned int p = 0; p < h->ports; p++)
      ehci_port(h->op + 0x11 + p, 0, 1U << 12);
    usb_delay_ms(100);
    /* Direct full/low-speed ports belong to UHCI/OHCI companions. */
    for (unsigned int p = 0; p < h->ports; p++) {
      volatile uint32_t *r = h->op + 0x11 + p;
      if (!(*r & 1))
        continue;
      ehci_port(r, 4, 1U << 8);
      usb_delay_ms(50);
      ehci_port(r, 1U << 8, 0);
      if (wait_bits(r, 1U << 8, 0, 100))
        continue;
      usb_delay_ms(10);
      if (!(*r & 4))
        ehci_port(r, 0, 1U << 13);
    }
  } else {
    h->op = cap;
    h->ports = cap[0x12] & 255;
    if (h->ports > 15)
      return -1;
    if (cap[1] & (1U << 8)) {
      cap[2] = 1U << 3;
      if (wait_bits(cap + 1, 1U << 8, 0, 1000))
        return -1;
    }
    unsigned int interval = cap[0xd] & 0x3fff;
    if (interval < 10000)
      interval = 11999;
    cap[5] = 0xffffffffU;
    cap[1] = 0;
    usb_delay_ms(50);
    cap[2] = 1;
    if (wait_bits(cap + 2, 1, 0, 100))
      return -1;
    cap[6] = (uint32_t)h->hcca;
    cap[8] = cap[10] = 0;
    cap[0xd] = interval | (((interval - 210) * 6 / 7) << 16) |
               ((cap[0xd] ^ (1U << 31)) & (1U << 31));
    cap[0x10] = interval * 9 / 10;
    cap[0x11] = 0x628;
    cap[0x12] =
        (cap[0x12] & ~(1U << 8)) | (1U << 9); /* No switched port power. */
    cap[0x14] = 1U << 16;
    cap[1] = 0x80;
    usb_delay_ms(100);
  }
  if (h->ports > (unsigned int)(PORTS - nport))
    h->ports = PORTS - nport;
  for (unsigned int n = 0; n < h->ports; n++) {
    Port *p = &ports[nport++];
    p->host = nhost;
    p->port = n;
  }
  /* Every frame visits the anchor; open pipes are linked behind it. An
   * S-mask of zero means the controller never executes the anchor itself. */
  h->anchor.link = 1;
  h->anchor.ep = 0;
  h->anchor.cap = 0;
  h->anchor.overlay.next = h->anchor.overlay.alt = 1;
  h->anchor.overlay.token = 1U << 6;
  h->anchor_ed.flags = 1U << 14;
  h->anchor_ed.next = 0;
  if (h->ports) {
    if (kind == 0x20) {
      for (int n = 0; n < 1024; n++)
        h->frames[n] = (uint32_t)&h->anchor | 2;
    } else {
      for (int n = 0; n < 32; n++)
        ((uint32_t *)h->hcca)[n] = (uint32_t)&h->anchor_ed;
      h->op[1] |= 4;
    }
  }
  h->live = 1;
  running[kind == 0x20]++;
  nhost++;
  return 0;
}
int usb_legacy_init(void) {
  if (initialized)
    return nport;
  initialized = 1;
  /* EHCI ownership/routing must precede companion initialization. */
  for (unsigned int kind = 0x20;; kind = 0x10) {
    for (unsigned int b = 0; b < 256; b++)
      for (unsigned int s = 0; s < 32; s++) {
        if ((pci(b, s, 0, 0) & 0xffff) == 0xffff)
          continue;
        unsigned int functions = pci(b, s, 0, 12) & 0x800000 ? 8 : 1;
        for (unsigned int f = 0;
             f < functions && nhost < HOSTS && nport < PORTS; f++) {
          if ((pci(b, s, f, 8) >> 8) != (0x0c0300 | kind))
            continue;
          detected[kind == 0x20]++;
          unsigned int bar =
              storage_pci_mmio_bar((b << 16) | (s << 11) | (f << 8), 0);
          if (!bar)
            continue;
          initialize(b, s, f, kind, bar & ~15U);
        }
      }
    if (kind == 0x10)
      break;
  }
  return nport;
}
unsigned int usb_legacy_status(int index) {
  if (index < 0 || index >= nport)
    return 0;
  Port *p = &ports[index];
  Host *h = &hosts[p->host];
  if (!h->live)
    return 0;
  unsigned int v = *port_reg(p);
  if (h->kind == 0x20) {
    if (v & (1U << 13))
      return 0;
    /* Newly attached low-speed devices can be handed off immediately. */
    if ((v & 1) && ((v >> 10) & 3) == 1) {
      ehci_port(port_reg(p), 0, 1U << 13);
      return 0;
    }
    return (v & 1) | (v & 2) | (v & 4);
  }
  return (v & 1) | ((v >> 15) & 2) | ((v & 2) << 1) | ((v >> 1) & 0x100);
}
void usb_legacy_ack(int index) {
  Port *p = &ports[index];
  Host *h = &hosts[p->host];
  volatile uint32_t *r = port_reg(p);
  if (h->kind == 0x20)
    ehci_port(r, 0, *r & 0x2a);
  else
    *r = *r & 0x1f0000;
}
int usb_legacy_reset(int index) {
  Port *p = &ports[index];
  Host *h = &hosts[p->host];
  volatile uint32_t *r = port_reg(p);
  if (!h->live || !(*r & 1))
    return -1;
  for (int n = 0; n < PIPES; n++)   /* a reset ends every pipe on the port */
    if (pipes[n].used && pipes[n].port == index)
      usb_legacy_pipe_close(n);
  usb_delay_ms(100);
  if (h->kind == 0x20) {
    ehci_port(r, 4, 1U << 8);
    usb_delay_ms(50);
    ehci_port(r, 1U << 8, 0);
    if (wait_bits(r, 1U << 8, 0, 100))
      return -1;
    usb_delay_ms(10);
    if (!(*r & 4)) {
      ehci_port(r, 0, 1U << 13);
      return -1;
    }
  } else {
    *r = 16;
    if (wait_bits(r, 16, 0, 100))
      return -1;
    usb_delay_ms(10);
    if (!(*r & 2))
      return -1;
  }
  usb_legacy_ack(index);
  return 0;
}
void usb_legacy_disable(int index) {
  Port *p = &ports[index];
  if (hosts[p->host].kind == 0x20)
    ehci_port(port_reg(p), 4, 0);
  else
    *port_reg(p) = 1;
}
static void fill_qtd(Qtd *t, unsigned int pid, unsigned int toggle,
                     unsigned char *data, unsigned int length) {
  memset(t, 0, sizeof(*t));
  t->next = t->alt = 1;
  t->buffer[0] = (uint32_t)data;
  for (int n = 1; n < 5; n++)
    t->buffer[n] = ((uint32_t)data & ~4095U) + n * 4096;
  t->token = (toggle << 31) | (length << 16) | (3U << 10) | (pid << 8) | 128;
}
static void fill_ot(Ot *t, unsigned int pid, unsigned int toggle,
                    unsigned char *data, unsigned int length) {
  memset(t, 0, 2 * sizeof(*t));
  t->flags = 0xf0000000U | ((2U | toggle) << 24) | (7U << 21) | (pid << 19) |
             (1U << 18);
  t->buffer = length ? (uint32_t)data : 0;
  t->end = length ? (uint32_t)data + length - 1 : 0;
  t->next = (uint32_t)(t + 1);
}
int usb_legacy_is_ehci(int index) {
  if (index < 0 || index >= nport)
    return -1;
  return hosts[ports[index].host].kind == 0x20;
}
/* EHCI endpoint characteristics and capabilities (QH dwords 1 and 2). */
static void ehci_route(EhciQh *qh, unsigned int address, unsigned int endpoint,
                       unsigned int packet, unsigned int route, int head,
                       int periodic) {
  unsigned int speed = route & 3;
  if (speed == USB_ROUTE_ROOT || speed == USB_SPEED_HIGH) {
    qh->ep = address | (endpoint << 8) | (2U << 12) | (1U << 14) |
             (head ? 1U << 15 : 0) | (packet << 16) | (periodic ? 0 : 4U << 28);
    qh->cap = (1U << 30) | (periodic ? 1 : 0);
    return;
  }
  /* Split transactions: no NAK reload, the control flag on endpoint 0,
   * and for interrupt endpoints a start-split in micro-frame 0 with
   * complete-splits in 2, 3 and 4. */
  qh->ep = address | (endpoint << 8) | (speed << 12) | (1U << 14) |
           (head ? 1U << 15 : 0) | (packet << 16) | (endpoint ? 0 : 1U << 27);
  qh->cap = (1U << 30) | ((route >> 16 & 127) << 23) | ((route >> 8 & 127) << 16) |
            (periodic ? (0x1cU << 8) | 1 : 0);
}
static unsigned int ohci_low(int index, unsigned int route) {
  if ((route & 3) == USB_ROUTE_ROOT)
    return (usb_legacy_status(index) & 0x100) ? 1U << 13 : 0;
  return (route & 3) == USB_SPEED_LOW ? 1U << 13 : 0;
}
int usb_legacy_transfer(int index, unsigned int address, unsigned int endpoint,
                        unsigned int packet, unsigned int pid,
                        unsigned char *toggle, void *data,
                        unsigned int length, unsigned int route) {
  if (index < 0 || index >= nport || !packet || length > 4096)
    return -1;
  Port *p = &ports[index];
  Host *h = &hosts[p->host];
  if (!h->live || (usb_legacy_status(index) & 5) != 5)
    return -1;
  int input = pid == 0x69;
  unsigned int result = 0;
  if (!input && length)
    memcpy(h->dma, data, length);
  if (h->kind == 0x20) {
    if (async_schedule(h, 0))
      return -1;
    fill_qtd(&h->qtd, pid == 0x2d ? 2 : input ? 1 : 0, *toggle, h->dma, length);
    ehci_route(&h->qh, address, endpoint, packet, route, 1, 0);
    h->qh.current = 0;
    memset(&h->qh.overlay, 0, sizeof(Qtd));
    h->qh.overlay.next = (uint32_t)&h->qtd;
    h->qh.overlay.alt = 1;
    barrier();
    if (async_schedule(h, 1))
      return -1;
    int timeout = wait_bits(&h->qtd.token, 128, 0, 1000);
    if (async_schedule(h, 0))
      return -1;
    if (timeout || (h->qtd.token & 0x7c))
      return -1;
    unsigned int residue = (h->qtd.token >> 16) & 0x7fff;
    if (residue > length)
      return -1;
    result = length - residue;
    *toggle = h->qtd.token >> 31;
  } else {
    h->op[1] &= ~0x30U;
    usb_delay_ms(2);
    fill_ot(h->td, pid == 0x2d ? 0 : input ? 2 : 1, *toggle, h->dma, length);
    h->ed.flags = address | (endpoint << 7) | ohci_low(index, route) |
                  (packet << 16);
    h->ed.tail = (uint32_t)&h->td[1];
    h->ed.head = (uint32_t)&h->td[0];
    h->ed.next = 0;
    int control = endpoint == 0;
    h->op[control ? 8 : 10] = (uint32_t)&h->ed;
    h->op[control ? 9 : 11] = 0;
    barrier();
    h->op[1] |= control ? 16 : 32;
    h->op[2] = control ? 2 : 4;
    int timeout = 1000;
    while ((h->td[0].flags >> 28) == 15 && timeout--)
      usb_delay_ms(1);
    h->ed.flags |= 1U << 14;
    h->op[1] &= ~0x30U;
    usb_delay_ms(2);
    if ((h->td[0].flags >> 28) != 0)
      return -1;
    result = h->td[0].buffer ? h->td[0].buffer - (uint32_t)h->dma : length;
    if (result > length)
      return -1;
    *toggle = (h->ed.head >> 1) & 1;
  }
  if (input && result)
    memcpy(data, h->dma, result);
  return result;
}
static void fill_pipe_qtd(Pipe *p, int k, unsigned int toggle, unsigned int packet) {
  Qtd *t = &p->qtd[k];
  t->token = 0;
  barrier();
  t->next = (uint32_t)&p->qtd[k ^ 1];
  t->alt = 1;
  t->buffer[0] = (uint32_t)p->dma[k];
  for (int n = 1; n < 5; n++)
    t->buffer[n] = ((uint32_t)p->dma[k] & ~4095U) + n * 4096;
  for (int n = 0; n < 5; n++)
    t->high[n] = 0;
  barrier();
  /* The active bit goes last: the controller may be reading this qTD. */
  t->token = (toggle << 31) | (packet << 16) | (3U << 10) | (1U << 8) | 128;
}
int usb_legacy_pipe(int index) {
  if (index < 0 || index >= nport)
    return -1;
  Host *h = &hosts[ports[index].host];
  if (!h->live)
    return -1;
  for (int n = 0; n < PIPES; n++) {
    Pipe *p = &pipes[n];
    if (p->used)
      continue;
    memset(p, 0, sizeof(*p));
    p->used = 1;
    p->port = index;
    if (h->kind == 0x20) {
      p->qh.overlay.next = p->qh.overlay.alt = 1;
      p->qh.overlay.token = 1U << 6;
      p->qh.link = h->anchor.link;
      barrier();
      h->anchor.link = (uint32_t)&p->qh | 2;
    } else {
      p->ed.flags = 1U << 14;
      p->ed.next = h->anchor_ed.next;
      barrier();
      h->anchor_ed.next = (uint32_t)&p->ed;
    }
    return n;
  }
  return -1;
}
void usb_legacy_pipe_close(int pipe) {
  if (pipe < 0 || pipe >= PIPES || !pipes[pipe].used)
    return;
  Pipe *p = &pipes[pipe];
  Host *h = &hosts[ports[p->port].host];
  if (h->kind == 0x20) {
    int on = h->live && !periodic_schedule(h, 0);
    for (EhciQh *q = &h->anchor; !(q->link & 1); q = (EhciQh *)(q->link & ~31U))
      if ((q->link & ~31U) == (uint32_t)&p->qh) { q->link = p->qh.link; break; }
    if (on)
      periodic_schedule(h, 1);
  } else {
    p->ed.flags |= 1U << 14;
    if (h->live)
      usb_delay_ms(2);
    for (Ed *e = &h->anchor_ed; e->next; e = (Ed *)e->next)
      if (e->next == (uint32_t)&p->ed) { e->next = p->ed.next; break; }
    if (h->live)
      usb_delay_ms(2);
  }
  p->used = 0;
}
unsigned int usb_legacy_pipe_status(int pipe) {
  return pipe >= 0 && pipe < PIPES ? pipes[pipe].status : 0;
}
/* (Re)starts a pipe from nothing: the slow path, at the start and after an
 * error halted the endpoint. */
static int pipe_start(Pipe *p, Host *h, unsigned int address, unsigned int endpoint,
                      unsigned int packet, unsigned int toggle, unsigned int route, int index) {
  if (h->kind == 0x20) {
    if (periodic_schedule(h, 0))
      return -1;
    fill_pipe_qtd(p, 0, toggle, packet);
    p->qtd[1].token = 0;
    p->qtd[1].next = (uint32_t)&p->qtd[0];
    p->qtd[1].alt = 1;
    ehci_route(&p->qh, address, endpoint, packet, route, 0, 1);
    p->qh.current = 0;
    memset((void *)&p->qh.overlay, 0, sizeof(Qtd));
    p->qh.overlay.next = (uint32_t)&p->qtd[0];
    p->qh.overlay.alt = 1;
    barrier();
    if (periodic_schedule(h, 1))
      return -1;
  } else {
    p->ed.flags |= 1U << 14;
    usb_delay_ms(2);
    fill_ot(p->td, 2, toggle, p->dma[0], packet);
    p->ed.flags = address | (endpoint << 7) | ohci_low(index, route) | (packet << 16) | (1U << 14);
    p->ed.tail = (uint32_t)&p->td[1];
    p->ed.head = (uint32_t)&p->td[0];
    barrier();
    p->ed.flags &= ~(1U << 14);
  }
  p->current = 0;
  p->started = 1;
  return 0;
}
/* Polls a mouse pipe: bytes received (>= 0), -2 nothing yet, -3 a transfer
 * error (usb_legacy_pipe_status tells which; the next call retries), -1 the
 * controller is gone. */
int usb_legacy_mouse(int pipe, unsigned int address, unsigned int endpoint,
                     unsigned int packet, unsigned char *toggle, void *data,
                     unsigned int route) {
  if (pipe < 0 || pipe >= PIPES || !pipes[pipe].used)
    return -1;
  Pipe *p = &pipes[pipe];
  Host *h = &hosts[ports[p->port].host];
  if (!h->live || !packet)
    return -1;
  if (packet > PIPE_PACKET)
    packet = PIPE_PACKET;
  if (!p->started)
    return pipe_start(p, h, address, endpoint, packet, *toggle, route, p->port) ? -1 : -2;
  int k = p->current, result;
  if (h->kind == 0x20) {
    uint32_t token = p->qtd[k].token;
    if (token & 128)
      return -2;
    /* Only a halt is an error: transaction errors and missed micro-frames
     * that the controller retried successfully leave their bits set. */
    if (token & 0x40) {
      p->status = token & 0xff;
      p->started = 0;
      return -3;
    }
    unsigned int residue = (token >> 16) & 0x7fff;
    if (residue > packet) {
      p->status = 0x100 | (token & 0xff);
      p->started = 0;
      return -3;
    }
    result = packet - residue;
    *toggle = token >> 31;
    if (result)
      memcpy(data, p->dma[k], result);
    /* The controller now waits on the other qTD: arm it. */
    fill_pipe_qtd(p, k ^ 1, *toggle, packet);
  } else {
    Ot *t = &p->td[k];
    unsigned int code = t->flags >> 28;
    if (code == 15)
      return -2;
    if (code) {
      p->status = 0x200 | code;
      p->started = 0;
      return -3;
    }
    result = t->buffer ? t->buffer - (uint32_t)p->dma[k] : packet;
    if ((unsigned int)result > packet) {
      p->status = 0x300;
      p->started = 0;
      return -3;
    }
    *toggle = (p->ed.head >> 1) & 1;
    if (result)
      memcpy(data, p->dma[k], result);
    /* The retired TD becomes the new dummy tail; the old dummy carries
     * the next transfer. */
    Ot *next = &p->td[k ^ 1];
    t->flags = 0; t->buffer = t->end = 0; t->next = 0;
    next->flags = 0xf0000000U | (7U << 21) | (2U << 19) | (1U << 18);   /* toggle from the ED */
    next->buffer = (uint32_t)p->dma[k ^ 1];
    next->end = (uint32_t)p->dma[k ^ 1] + packet - 1;
    next->next = (uint32_t)t;
    barrier();
    p->ed.tail = (uint32_t)t;
  }
  p->current = k ^ 1;
  return result;
}

void usb_legacy_diagnostics(void) {
  usb_legacy_init();
  for (int n = 0; n < 2; n++) {
    print(WHITE, n ? ", EHCI " : ", OHCI ");
    print_uint(WHITE, running[n]);
    print(WHITE, "/");
    print_uint(WHITE, detected[n]);
  }
}
