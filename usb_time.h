#ifndef BARNIX_USB_TIME_H
#define BARNIX_USB_TIME_H
/* USB reset, debounce and power-on delays are wall-clock requirements. A
 * compiler/CPU dependent spin count is not a millisecond on physical PCs.
 * PIT channel 2 is also used by keyboard_sleep; these polling paths are serial.
 */
static inline unsigned char usb_timer_in(unsigned short p) {
  unsigned char v;
  __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"(p));
  return v;
}
static inline void usb_timer_out(unsigned short p, unsigned char v) {
  __asm__ volatile("outb %0,%1" ::"a"(v), "Nd"(p) : "memory");
}
static inline int usb_delay_ms(unsigned int ms) {
  while (ms) {
    unsigned int part = ms > 20 ? 20 : ms,
                 count = (1193182U * part + 999) / 1000;
    unsigned char old = usb_timer_in(0x61);
    usb_timer_out(0x61, old & ~3U);
    usb_timer_out(0x43, 0xb0);
    usb_timer_out(0x42, count & 255);
    usb_timer_out(0x42, count >> 8);
    usb_timer_out(0x61, (old & ~2U) | 1U);
    unsigned int limit = 10000000;
    while (!(usb_timer_in(0x61) & 32) && --limit) {
    }
    usb_timer_out(0x61, old);
    if (!limit)
      return -1;
    ms -= part;
  }
  return 0;
}
#endif
