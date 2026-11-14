#ifndef BARNIX_TIMER_H
#define BARNIX_TIMER_H
/* Wall-clock delays from the CPU time stamp counter. PIT channel 2 alone is
 * not enough: Alder Lake and newer Intel boards ship with the 8254 clock
 * gated, so it never counts and every PIT wait ran into its guard. */
unsigned int timer_tsc_per_ms(void);    /* 0 without a TSC */
void timer_delay_ms(unsigned int ms);
void timer_delay_us(unsigned int us);
#endif
