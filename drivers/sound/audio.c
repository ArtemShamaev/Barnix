/* Sound: a small software mixer feeding Intel High Definition Audio or AC'97
 * by DMA, plus the PC speaker for tones. Barnix runs without interrupts, so
 * the cyclic DMA buffer is refilled from audio_update(), which frame
 * presentation and input polling call; a short stall plays silence. */
#include "audio.h"
#include "storage_pci.h"
#include "linux_memory.h"
#include "memory.h"
#include "barnix.h"
#include "usb_time.h"

#define RATE 48000
#define RING_FRAMES 32768U          /* 0.68 s of 16-bit stereo */
#define AHEAD 4800                  /* frames mixed in advance: 100 ms */
#define GUARD 512                   /* never write where the DMA engine reads */
#define VOICES 16
#define AC97_ENTRIES 32

enum { NONE, HDA, AC97 };
typedef struct {
    const unsigned char *data;
    unsigned int frames, position, fraction, step;
    int left, right, generation;
    unsigned char channels, bits, loop, active;
} Voice;

static int card, master = 256, speaker_hz;
static char card_name[64] = "PC speaker";
static short *ring;
static unsigned int last_position, fill, queued, running;
static Voice voices[VOICES];
static int voice_generation;
static unsigned int tsc_per_ms;
static int has_clflush;

/* Real chipsets may read the DMA buffers without snooping the processor
 * caches (QEMU always sees memory): write the lines back after each change. */
static void flush(const void *start, unsigned int bytes)
{
    if (!bytes) return;
    if (!has_clflush) { __asm__ volatile("wbinvd" ::: "memory"); return; }
    for (unsigned int p = (unsigned int)(uintptr_t)start & ~63U, end = (unsigned int)(uintptr_t)start + bytes; p < end; p += 64)
        __asm__ volatile("clflush (%0)" :: "r"(p) : "memory");
    __asm__ volatile("mfence" ::: "memory");
}

static unsigned int tsc_low(void) { unsigned int lo, hi; __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi)); return lo; }

static inline void out8(unsigned short p, unsigned char v) { __asm__ volatile("outb %0,%1" :: "a"(v), "Nd"(p)); }
static inline void out16(unsigned short p, unsigned short v) { __asm__ volatile("outw %0,%1" :: "a"(v), "Nd"(p)); }
static inline void out32(unsigned short p, unsigned int v) { __asm__ volatile("outl %0,%1" :: "a"(v), "Nd"(p)); }
static inline unsigned char in8(unsigned short p) { unsigned char v; __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static inline unsigned short in16(unsigned short p) { unsigned short v; __asm__ volatile("inw %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static inline unsigned int in32(unsigned short p) { unsigned int v; __asm__ volatile("inl %1,%0" : "=a"(v) : "Nd"(p)); return v; }

static unsigned int pci_read(unsigned int address, unsigned int reg)
{ out32(0xcf8, address | (reg & 0xfc)); return in32(0xcfc); }
static void pci_write(unsigned int address, unsigned int reg, unsigned int value)
{ out32(0xcf8, address | (reg & 0xfc)); out32(0xcfc, value); }

static void append_text(char *out, const char *text)
{
    unsigned int n = (unsigned int)strlen(out);
    while (*text && n + 1 < sizeof(card_name)) out[n++] = *text++;
    out[n] = 0;
}
static void append_hex(char *out, unsigned int value, int digits)
{
    char text[9];
    for (int i = 0; i < digits; i++) text[i] = "0123456789ABCDEF"[value >> (4 * (digits - 1 - i)) & 15];
    text[digits] = 0;
    append_text(out, text);
}

/* ------------------------------------------------------------ Intel HDA */
static volatile unsigned char *hda;
static volatile unsigned int *corb, *rirb;
static unsigned int corb_entries, rirb_entries, corb_wp, rirb_rp, immediate, timeouts;
static volatile unsigned char *stream;   /* first output stream descriptor */
static unsigned int *bdl;

#define R8(o) (*(volatile unsigned char *)(hda + (o)))
#define R16(o) (*(volatile unsigned short *)(hda + (o)))
#define R32(o) (*(volatile unsigned int *)(hda + (o)))

static int wait_bits8(volatile unsigned char *reg, unsigned char mask, unsigned char want)
{
    for (int i = 0; i < 1000; i++) {
        if ((*reg & mask) == want) return 0;
        usb_delay_ms(1);
    }
    return -1;
}

/* One verb, one response; ~0 when the codec does not answer. */
static unsigned int hda_verb(unsigned int verb)
{
    if (timeouts >= 3) return ~0U;   /* a silent codec must not stall the boot */
    if (!immediate) {
        corb_wp = (corb_wp + 1) % corb_entries;
        corb[corb_wp] = verb;
        flush((const void *)&corb[corb_wp], 4);
        R16(0x48) = (unsigned short)corb_wp;
        for (unsigned int spin = 0; spin < 2000000; spin++) {
            unsigned int wp = R16(0x58) & 0xff;
            while (rirb_rp != wp % rirb_entries) {
                rirb_rp = (rirb_rp + 1) % rirb_entries;
                flush((const void *)&rirb[rirb_rp * 2], 8);   /* drop a stale line before reading */
                if (!(rirb[rirb_rp * 2 + 1] & 16)) return rirb[rirb_rp * 2];   /* skip unsolicited */
            }
            __asm__ volatile("pause");
        }
        immediate = 1;   /* the ring does not move: try the immediate interface */
    }
    for (unsigned int spin = 0; spin < 2000000 && (R16(0x68) & 1); spin++) __asm__ volatile("pause");
    R16(0x68) = 2;
    R32(0x60) = verb;
    R16(0x68) = 1;
    for (unsigned int spin = 0; spin < 2000000; spin++)
        if ((R16(0x68) & 3) == 2) return R32(0x64);
    timeouts++;
    return ~0U;
}
static unsigned int verb12(int codec, int node, unsigned int verb, unsigned int data)
{ return hda_verb((unsigned int)codec << 28 | (unsigned int)node << 20 | verb << 8 | (data & 255)); }
static unsigned int verb4(int codec, int node, unsigned int verb, unsigned int data)
{ return hda_verb((unsigned int)codec << 28 | (unsigned int)node << 20 | verb << 16 | (data & 0xffff)); }
static unsigned int param(int codec, int node, unsigned int id) { return verb12(codec, node, 0xf00, id); }

#define MAX_NODES 128
static unsigned int widget_caps[MAX_NODES], out_amp[MAX_NODES], in_amp[MAX_NODES];
static int first_node, node_count, used_dac[MAX_NODES];

static int widget_type(int node) { return widget_caps[node - first_node] >> 20 & 15; }
static int connections(int codec, int node, int *list, int capacity)
{
    if (!(widget_caps[node - first_node] & (1U << 8))) return 0;
    unsigned int length = param(codec, node, 0x0e), count = length & 127, longform = length & 128, n = 0;
    for (unsigned int i = 0; i < count && (int)n < capacity;) {
        unsigned int entry = verb12(codec, node, 0xf02, i);
        int per = longform ? 2 : 4, bits = longform ? 16 : 8;
        for (int k = 0; k < per && i < count && (int)n < capacity; k++, i++) {
            unsigned int value = entry >> (k * bits) & (longform ? 0xffffU : 0xffU);
            int id = (int)(value & (longform ? 0x7fffU : 0x7fU));
            if ((value & (longform ? 0x8000U : 0x80U)) && n) {   /* range from the previous entry */
                for (int r = list[n - 1] + 1; r <= id && (int)n < capacity; r++) list[n++] = r;
            } else list[n++] = id;
        }
    }
    return (int)n;
}
/* 0 dB, or the loudest step when the amplifier has no 0 dB point. */
static void unmute(int codec, int node, int output, int index)
{
    unsigned int caps = output ? out_amp[node - first_node] : in_amp[node - first_node];
    unsigned int gain = caps & 127;
    if (!gain) gain = caps >> 8 & 127;
    verb4(codec, node, 3, (output ? 0x8000U : 0x4000U) | 0x3000U | (unsigned int)index << 8 | gain);
}
/* Find a DAC behind node and route it; returns the DAC or 0. */
static int route(int codec, int node, int depth)
{
    int type = widget_type(node);
    if (type == 0) {
        if (widget_caps[node - first_node] & (1U << 9)) return 0;   /* digital (HDMI/S/PDIF) */
        verb12(codec, node, 0x705, 0);
        unmute(codec, node, 1, 0);
        return node;
    }
    if (depth > 4 || (type != 2 && type != 3 && type != 4)) return 0;
    int list[32], count = connections(codec, node, list, 32);
    for (int pass = 0; pass < 2; pass++)          /* prefer an unused, direct DAC */
        for (int i = 0; i < count; i++) {
            int next = list[i];
            if (next < first_node || next >= first_node + node_count) continue;
            if (pass == 0 && (widget_type(next) != 0 || used_dac[next - first_node])) continue;
            int dac = route(codec, next, depth + 1);
            if (!dac) continue;
            verb12(codec, node, 0x705, 0);
            if (type == 2) unmute(codec, node, 0, i);               /* mixer input */
            else verb12(codec, node, 0x701, (unsigned int)i);       /* selector or pin */
            if (widget_caps[node - first_node] & 4) unmute(codec, node, 1, 0);
            return dac;
        }
    return 0;
}
static void codec_name(unsigned int vendor)
{
    unsigned int maker = vendor >> 16, part = vendor & 0xffff;
    append_text(card_name, ", ");
    if (maker == 0x10ec) { append_text(card_name, "Realtek ALC"); append_hex(card_name, part, 3); return; }
    const char *name = maker == 0x1af4 ? "QEMU codec" : maker == 0x14f1 ? "Conexant" : maker == 0x111d || maker == 0x92d1 ? "IDT" :
                       maker == 0x1106 ? "VIA" : maker == 0x11d4 ? "Analog Devices" : maker == 0x1013 ? "Cirrus Logic" :
                       maker == 0x8384 ? "SigmaTel" : maker == 0x1002 ? "AMD" : maker == 0x8086 ? "Intel" : 0;
    if (name) append_text(card_name, name);
    else { append_text(card_name, "codec "); append_hex(card_name, vendor, 8); }
}
/* Route every analog output pin (line out, speaker, headphones) of codec. */
static int hda_codec(int codec)
{
    unsigned int nodes = param(codec, 0, 4);
    int afg = 0;
    for (unsigned int n = nodes >> 16 & 255, end = n + (nodes & 255); n < end && n < 256; n++)
        if ((param(codec, (int)n, 5) & 255) == 1) { afg = (int)n; break; }
    if (!afg) return 0;
    verb12(codec, afg, 0x705, 0);                 /* D0 */
    usb_delay_ms(10);
    unsigned int group_out = param(codec, afg, 0x12), group_in = param(codec, afg, 0x0d);
    unsigned int sub = param(codec, afg, 4);
    first_node = (int)(sub >> 16 & 255); node_count = (int)(sub & 255);
    if (node_count > MAX_NODES) node_count = MAX_NODES;
    for (int i = 0; i < node_count; i++) {
        int node = first_node + i;
        widget_caps[i] = param(codec, node, 9);
        out_amp[i] = widget_caps[i] & 8 ? param(codec, node, 0x12) : group_out;
        in_amp[i] = widget_caps[i] & 8 ? param(codec, node, 0x0d) : group_in;
        used_dac[i] = 0;
    }
    int routed = 0;
    for (int strict = 1; strict >= 0 && !routed; strict--)
        for (int i = 0; i < node_count; i++) {
            int node = first_node + i;
            if (widget_type(node) != 4 || (widget_caps[i] & (1U << 9))) continue;
            unsigned int caps = param(codec, node, 0x0c), config = verb12(codec, node, 0xf1c, 0);
            unsigned int device = config >> 20 & 15, connected = config >> 30;
            if (!(caps & 16)) continue;                            /* not output capable */
            if (strict && (connected == 1 || device > 2)) continue;
            int dac = route(codec, node, 0);
            if (!dac) continue;
            used_dac[dac - first_node] = 1;
            verb12(codec, node, 0x707, 0x40U | (device == 2 && (caps & 8) ? 0x80U : 0));
            if (caps & (1U << 16)) verb12(codec, node, 0x70c, 2);   /* external amplifier on */
            verb4(codec, dac, 2, 0x0011);                           /* 48 kHz, 16 bit, stereo */
            verb12(codec, dac, 0x706, 0x10);                        /* stream 1, channel 0 */
            routed++;
        }
    if (routed) codec_name(param(codec, 0, 0));
    return routed;
}
static int hda_start(unsigned int pci, unsigned int bar)
{
    /* Sounds (key clicks) also play inside Linux system calls, under their
     * page tables: the registers must be mapped there too. */
    if (linux_memory_map_mmio(bar, 0x4000)) return 0;
    hda = (volatile unsigned char *)(uintptr_t)bar;
    pci_write(pci, 4, (pci_read(pci, 4) & 0xffff) | 6);   /* memory, bus master */
    /* As Linux does: traffic class 0 (TCSEL), and snooped DMA where the
     * chipset makes it optional (Intel PCH DEVC, ATI/AMD, NVIDIA). */
    unsigned int vendor = pci_read(pci, 0) & 0xffff;
    pci_write(pci, 0x44, pci_read(pci, 0x44) & ~7U);
    if (vendor == 0x8086) pci_write(pci, 0x78, pci_read(pci, 0x78) & ~(1U << 11));
    unsigned int device_id = pci_read(pci, 0) >> 16;
    if ((vendor == 0x1002 && (device_id == 0x437b || device_id == 0x4383)) || vendor == 0x1022) pci_write(pci, 0x40, (pci_read(pci, 0x40) & ~(7U << 16)) | (2U << 16));
    if (vendor == 0x10de) {
        pci_write(pci, 0x4c, (pci_read(pci, 0x4c) & ~(15U << 16)) | (15U << 16));
        pci_write(pci, 0x4c, pci_read(pci, 0x4c) | 1U | 1U << 8);
    }
    unsigned char *dma = memory_kernel_alloc(4096 * 2 + RING_FRAMES * 4);
    if (!dma) return 0;
    corb = (volatile unsigned int *)dma; rirb = (volatile unsigned int *)(dma + 1024);
    bdl = (unsigned int *)(dma + 4096); ring = (short *)(dma + 8192);

    R32(0x08) &= ~1U;                                     /* controller reset */
    if (wait_bits8(hda + 0x08, 1, 0)) return 0;
    usb_delay_ms(1);
    R32(0x08) |= 1U;
    if (wait_bits8(hda + 0x08, 1, 1)) return 0;
    usb_delay_ms(2);
    unsigned int codecs = R16(0x0e);
    if (!codecs) { usb_delay_ms(20); codecs = R16(0x0e); }
    R32(0x20) = 0;                                        /* no interrupts */

    R8(0x4c) = 0; R8(0x5c) = 0;                           /* CORB / RIRB stopped */
    usb_delay_ms(1);
    unsigned int size = R8(0x4e);
    corb_entries = size & 0x40 ? 256 : size & 0x20 ? 16 : 2;
    R8(0x4e) = (unsigned char)((size & 0xfc) | (corb_entries == 256 ? 2 : corb_entries == 16 ? 1 : 0));
    size = R8(0x5e);
    rirb_entries = size & 0x40 ? 256 : size & 0x20 ? 16 : 2;
    R8(0x5e) = (unsigned char)((size & 0xfc) | (rirb_entries == 256 ? 2 : rirb_entries == 16 ? 1 : 0));
    memset((void *)corb, 0, 4096);
    flush((const void *)corb, 4096);
    R32(0x40) = (unsigned int)(uintptr_t)corb; R32(0x44) = 0;
    R32(0x50) = (unsigned int)(uintptr_t)rirb; R32(0x54) = 0;
    R16(0x4a) = 0x8000;                                   /* CORB read pointer reset */
    for (int i = 0; i < 1000 && !(R16(0x4a) & 0x8000); i++) __asm__ volatile("pause");
    R16(0x4a) = 0;
    for (int i = 0; i < 100000 && (R16(0x4a) & 0x8000); i++) __asm__ volatile("pause");
    R16(0x48) = 0; corb_wp = 0;
    R16(0x58) = 0x8000; rirb_rp = 0;
    R16(0x5a) = 1;
    R8(0x4c) = 2; R8(0x5c) = 2;                           /* run both rings */
    usb_delay_ms(1);
    immediate = 0; timeouts = 0;

    strcpy(card_name, "Intel HD Audio");
    int routed = 0;
    for (int codec = 0; codec < 15; codec++)
        if (codecs & (1U << codec)) routed += hda_codec(codec);
    if (!routed) return 0;
    char count[4] = {(char)('0' + (routed > 9 ? 9 : routed)), 0};
    append_text(card_name, ", outputs: "); append_text(card_name, count);

    unsigned int inputs = R16(0x00) >> 8 & 15;
    stream = hda + 0x80 + inputs * 0x20;
    stream[0] = 1;                                        /* stream reset */
    wait_bits8(stream, 1, 1);
    stream[0] = 0;
    wait_bits8(stream, 1, 0);
    unsigned int part = RING_FRAMES * 4 / 4;
    for (int i = 0; i < 4; i++) {
        bdl[i * 4] = (unsigned int)(uintptr_t)ring + i * part; bdl[i * 4 + 1] = 0;
        bdl[i * 4 + 2] = part; bdl[i * 4 + 3] = 0;
    }
    flush(bdl, 64);
    memset(ring, 0, RING_FRAMES * 4);
    flush(ring, RING_FRAMES * 4);
    *(volatile unsigned int *)(stream + 0x08) = RING_FRAMES * 4;   /* cyclic buffer length */
    *(volatile unsigned short *)(stream + 0x0c) = 3;               /* last valid entry */
    *(volatile unsigned short *)(stream + 0x12) = 0x0011;
    *(volatile unsigned int *)(stream + 0x18) = (unsigned int)(uintptr_t)bdl;
    *(volatile unsigned int *)(stream + 0x1c) = 0;
    stream[2] = 0x10;                                     /* stream tag 1 */
    stream[3] = 0x1c;                                     /* clear status */
    stream[0] = 2;                                        /* run */
    return 1;
}

/* ------------------------------------------------------------ AC'97 */
static unsigned short mixer_io, bus_io;
static int ac97_start(unsigned int pci)
{
    unsigned int bar0 = pci_read(pci, 0x10), bar1 = pci_read(pci, 0x14);
    if (!(bar0 & 1) || !(bar1 & 1)) return 0;
    mixer_io = (unsigned short)(bar0 & ~3U); bus_io = (unsigned short)(bar1 & ~3U);
    pci_write(pci, 4, (pci_read(pci, 4) & 0xffff) | 5);   /* I/O, bus master */
    unsigned char *dma = memory_kernel_alloc(4096 + RING_FRAMES * 4);
    if (!dma) return 0;
    bdl = (unsigned int *)dma; ring = (short *)(dma + 4096);
    out32(bus_io + 0x2c, 0);                              /* cold reset */
    usb_delay_ms(1);
    out32(bus_io + 0x2c, 2);
    int ready = 0;
    for (int i = 0; i < 600 && !ready; i++) { if (in32(bus_io + 0x30) & 0x100) ready = 1; else usb_delay_ms(1); }
    if (!ready) return 0;
    out16(mixer_io + 0x00, 0);                            /* mixer reset */
    usb_delay_ms(5);
    out16(mixer_io + 0x02, 0);                            /* master: 0 dB, unmuted */
    out16(mixer_io + 0x04, 0);                            /* headphones */
    out16(mixer_io + 0x18, 0x0808);                       /* PCM out: 0 dB */
    if (in16(mixer_io + 0x28) & 1) {                      /* variable rate */
        out16(mixer_io + 0x2a, in16(mixer_io + 0x2a) | 1);
        out16(mixer_io + 0x2c, RATE);
    }
    out8(bus_io + 0x1b, 2);                               /* PCM out reset */
    for (int i = 0; i < 100 && (in8(bus_io + 0x1b) & 2); i++) usb_delay_ms(1);
    unsigned int samples = RING_FRAMES * 2 / AC97_ENTRIES;
    for (int i = 0; i < AC97_ENTRIES; i++) {
        bdl[i * 2] = (unsigned int)(uintptr_t)ring + i * samples * 2;
        bdl[i * 2 + 1] = samples;
    }
    out32(bus_io + 0x10, (unsigned int)(uintptr_t)bdl);
    out8(bus_io + 0x15, AC97_ENTRIES - 1);
    out16(bus_io + 0x16, 0x1c);
    out8(bus_io + 0x1b, 1);                               /* run */
    strcpy(card_name, "AC'97 audio");
    unsigned int vendor = (unsigned int)in16(mixer_io + 0x7c) << 16 | in16(mixer_io + 0x7e);
    unsigned int maker = vendor >> 16;
    const char *name = maker == 0x8384 ? "SigmaTel" : maker == 0x414c ? "Realtek" : maker == 0x4144 ? "Analog Devices" :
                       maker == 0x4352 ? "Cirrus Logic" : maker == 0x5649 ? "VIA" : maker == 0x434d ? "C-Media" :
                       maker == 0x594d ? "Yamaha" : 0;
    if (name) { append_text(card_name, ", "); append_text(card_name, name); }
    else if (vendor && vendor != 0xffffffffU) { append_text(card_name, ", codec "); append_hex(card_name, vendor, 8); }
    return 1;
}

/* ------------------------------------------------------------ position */
static unsigned int hardware_position(void)
{
    if (card == HDA) return (*(volatile unsigned int *)(stream + 0x04) / 4) % RING_FRAMES;
    unsigned int frames = RING_FRAMES / AC97_ENTRIES;
    unsigned int index = in8(bus_io + 0x14) & 31, left = in16(bus_io + 0x18);
    unsigned int position = index * frames + frames - (left / 2 > frames ? frames : left / 2);
    /* Keep the engine one lap ahead so it never reaches the last entry. */
    out8(bus_io + 0x15, (unsigned char)((index + AC97_ENTRIES - 1) & 31));
    if (in16(bus_io + 0x16) & 1) {                        /* halted: restart */
        out16(bus_io + 0x16, 0x1c);
        out8(bus_io + 0x1b, 1);
    }
    return position % RING_FRAMES;
}

static int has_tsc(void)
{
    unsigned int a, b;
    __asm__ volatile("pushfl; pushfl; popl %0; movl %0,%1; xorl $0x200000,%0; pushl %0; popfl; pushfl; popl %0; popfl"
                     : "=&r"(a), "=&r"(b));
    if (!((a ^ b) & 0x200000)) return 0;
    unsigned int c, d;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1), "c"(0));
    return (d & 16) != 0;
}

void audio_init(void)
{
    card = NONE; ring = 0;
    if (has_tsc()) {
        unsigned int a, b, c, d;
        __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1), "c"(0));
        has_clflush = (d >> 19) & 1;
        tsc_per_ms = timer_tsc_per_ms();
    }
    for (unsigned int bus = 0; bus < 256 && card == NONE; bus++)
        for (unsigned int slot = 0; slot < 32 && card == NONE; slot++)
            for (unsigned int fn = 0; fn < 8 && card == NONE; fn++) {
                unsigned int address = 0x80000000U | bus << 16 | slot << 11 | fn << 8;
                unsigned int id = pci_read(address, 0);
                if ((id & 0xffff) == 0xffff) { if (!fn) break; continue; }
                unsigned int class = pci_read(address, 8) >> 8;
                /* Intel's audio DSP (SOF, class 0401 on Alder Lake laptops)
                 * still has the HD Audio controller inside. */
                int intel_dsp = class >> 8 == 0x0401 && (id & 0xffff) == 0x8086 && (class & 0xff) == 0;
                if (class >> 8 == 0x0403 || intel_dsp) {
                    unsigned int bar = pci_read(address, 0x10);
                    if ((bar & 6) == 4 && pci_read(address, 0x14))   /* above 4 GiB (UEFI): moved below */
                        bar = storage_pci_mmio_bar(address & 0x00ffff00U, 0);
                    if (!(bar & 1) && (bar & ~15U) && hda_start(address, bar & ~15U)) card = HDA;
                }
                if (card == NONE && class >> 8 == 0x0401 && !intel_dsp && ac97_start(address)) card = AC97;
                if (!fn && !(pci_read(address, 12) & 0x800000)) break;   /* single function */
            }
    if (card == NONE) { ring = 0; strcpy(card_name, "PC speaker"); return; }
    memset(ring, 0, RING_FRAMES * 4);
    last_position = hardware_position();
    fill = (last_position + GUARD) % RING_FRAMES; queued = GUARD; running = 1;
}

/* ------------------------------------------------------------ mixer */
/* Adds count frames of voice into the ring at frame at (no wrap). */
static void add_voice(Voice *voice, unsigned int at, unsigned int count)
{
    short *out = ring + at * 2;
    int left = voice->left * master >> 8, right = voice->right * master >> 8;
    for (unsigned int f = 0; f < count && voice->active; f++) {
        int l, r;
        unsigned int p = voice->position;
        if (voice->bits == 16) {
            const short *sample = (const short *)voice->data + p * voice->channels;
            l = sample[0]; r = voice->channels == 2 ? sample[1] : l;
        } else {
            const unsigned char *sample = voice->data + p * voice->channels;
            l = (sample[0] - 128) << 8; r = voice->channels == 2 ? (sample[1] - 128) << 8 : l;
        }
        l = out[f * 2] + (l * left >> 8); r = out[f * 2 + 1] + (r * right >> 8);
        out[f * 2] = (short)(l > 32767 ? 32767 : l < -32768 ? -32768 : l);
        out[f * 2 + 1] = (short)(r > 32767 ? 32767 : r < -32768 ? -32768 : r);
        voice->fraction += voice->step;
        voice->position += voice->fraction >> 16;
        voice->fraction &= 0xffff;
        if (voice->position >= voice->frames) {
            if (voice->loop) voice->position %= voice->frames;
            else voice->active = 0;
        }
    }
}
static void mix(unsigned int at, unsigned int count)
{
    memset(ring + at * 2, 0, (int)count * 4);
    for (int v = 0; v < VOICES; v++) if (voices[v].active) add_voice(&voices[v], at, count);
    flush(ring + at * 2, count * 4);
}

/* The speaker shares PIT channel 2 with the delay loops: re-arm it when a
 * delay left the counter in another mode. */
static void speaker_program(unsigned int hz)
{
    unsigned int divisor = 1193182U / hz;
    if (divisor > 65535) divisor = 65535;
    if (divisor < 1) divisor = 1;
    out8(0x43, 0xb6);
    out8(0x42, (unsigned char)divisor);
    out8(0x42, (unsigned char)(divisor >> 8));
    out8(0x61, in8(0x61) | 3);
}

void audio_update(void)
{
    if (speaker_hz) {
        out8(0x43, 0xe8);                                 /* read back channel 2 status */
        if ((in8(0x42) >> 1 & 3) != 3 || (in8(0x61) & 3) != 3) speaker_program((unsigned int)speaker_hz);
    }
    if (!running) return;
    unsigned int position = hardware_position();
    unsigned int advanced = (position + RING_FRAMES - last_position) % RING_FRAMES;
    /* Silence what has been played, so a stall repeats nothing old. */
    for (unsigned int f = last_position, n = 0; n < advanced; n++, f = (f + 1) % RING_FRAMES)
        ring[f * 2] = ring[f * 2 + 1] = 0;
    if (last_position + advanced <= RING_FRAMES) flush(ring + last_position * 2, advanced * 4);
    else { flush(ring + last_position * 2, (RING_FRAMES - last_position) * 4); flush(ring, (last_position + advanced - RING_FRAMES) * 4); }
    last_position = position;
    if (advanced >= queued) {                             /* underrun: start just ahead */
        fill = (position + GUARD) % RING_FRAMES;
        queued = GUARD;
    } else queued -= advanced;
    while (queued < AHEAD) {
        unsigned int count = AHEAD - queued;
        if (count > RING_FRAMES - fill) count = RING_FRAMES - fill;
        mix(fill, count);
        fill = (fill + count) % RING_FRAMES;
        queued += count;
    }
}

int audio_sleep(unsigned int milliseconds)
{
    if (!speaker_hz || !tsc_per_ms) return 0;
    while (milliseconds) {
        unsigned int part = milliseconds > 100 ? 100 : milliseconds, start = tsc_low();
        milliseconds -= part;
        while (tsc_low() - start < part * tsc_per_ms) { audio_update(); __asm__ volatile("pause"); }
    }
    return 1;
}

int audio_device(char *name, unsigned int capacity)
{
    if (name && capacity) {
        unsigned int i = 0;
        for (; card_name[i] && i + 1 < capacity; i++) name[i] = card_name[i];
        name[i] = 0;
    }
    return card == NONE ? BARNIX_SOUND_SPEAKER : BARNIX_SOUND_CARD;
}

void audio_speaker(unsigned int hz)
{
    if (hz < 20 || hz > 20000) {
        speaker_hz = 0;
        out8(0x61, in8(0x61) & ~3U);
        return;
    }
    speaker_hz = (int)hz;
    speaker_program(hz);
}

int audio_play(const BarnixSound *sound)
{
    if (!sound || !sound->data || !sound->frames || card == NONE ||
        (sound->channels != 1 && sound->channels != 2) || (sound->bits != 8 && sound->bits != 16) ||
        sound->rate < 4000 || sound->rate > 96000) return -1;
    audio_update();
    int slot = -1;
    for (int v = 0; v < VOICES && slot < 0; v++) if (!voices[v].active) slot = v;
    if (slot < 0) {                                       /* replace the oldest one-shot */
        for (int v = 0; v < VOICES; v++)
            if (!voices[v].loop && (slot < 0 || voices[v].generation < voices[slot].generation)) slot = v;
        if (slot < 0) return -1;
    }
    Voice *voice = &voices[slot];
    voice->active = 0;
    int volume = sound->volume < 0 ? 0 : sound->volume > 256 ? 256 : sound->volume;
    int pan = sound->pan < -256 ? -256 : sound->pan > 256 ? 256 : sound->pan;
    voice->data = sound->data; voice->frames = sound->frames;
    voice->position = voice->fraction = 0;
    voice->step = sound->rate * 1024U / 750U;            /* rate / 48000 in 16.16 */
    voice->left = volume * (pan > 0 ? 256 - pan : 256) >> 8;
    voice->right = volume * (pan < 0 ? 256 + pan : 256) >> 8;
    voice->channels = sound->channels; voice->bits = sound->bits; voice->loop = sound->loop;
    voice->generation = ++voice_generation & 0x7fffff;
    voice->active = 1;
    /* The next 100 ms are mixed already: add the clip there so it starts now. */
    if (running && queued > GUARD) {
        unsigned int at = (last_position + GUARD) % RING_FRAMES, count = queued - GUARD;
        while (count && voice->active) {
            unsigned int part = count > RING_FRAMES - at ? RING_FRAMES - at : count;
            add_voice(voice, at, part);
            flush(ring + at * 2, part * 4);
            at = (at + part) % RING_FRAMES; count -= part;
        }
    }
    return voice->generation << 4 | slot;
}

void audio_stop(int id)
{
    for (int v = 0; v < VOICES; v++)
        if (id < 0 || (voices[v].active && (id & 15) == v && id >> 4 == voices[v].generation)) voices[v].active = 0;
}

int audio_playing(int id)
{
    if (id < 0) return 0;
    audio_update();
    Voice *voice = &voices[id & 15];
    return voice->active && id >> 4 == voice->generation;
}

void audio_volume(int volume) { master = volume < 0 ? 0 : volume > 256 ? 256 : volume; }

void audio_reset(void)
{
    audio_stop(-1);
    audio_speaker(0);
    master = 256;
    audio_update();
}
