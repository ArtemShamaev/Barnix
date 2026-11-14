#ifndef BARNIX_USB_TIME_H
#define BARNIX_USB_TIME_H
/* USB reset, debounce and power-on delays are wall-clock requirements. A
 * compiler/CPU dependent spin count is not a millisecond on physical PCs:
 * they come from the calibrated TSC (timer.c). */
#include "timer.h"
static inline int usb_delay_ms(unsigned int ms) {
  timer_delay_ms(ms);
  return 0;
}
#endif
