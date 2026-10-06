#ifndef BARNIX_MOUSE_H
#define BARNIX_MOUSE_H
/* Logical coordinates shared by PS/2 and USB boot-protocol mice. */
#define BARNIX_MOUSE_WIDTH 640
#define BARNIX_MOUSE_HEIGHT 400
#define BARNIX_MOUSE_LEFT 1U
#define BARNIX_MOUSE_RIGHT 2U
#define BARNIX_MOUSE_MIDDLE 4U
#define BARNIX_MOUSE_BACK 8U
#define BARNIX_MOUSE_FORWARD 16U
/* sequence changes on movement or button transitions; available indicates
 * that a pointer has been detected since initialization. */
typedef struct { int x,y; unsigned int buttons,sequence; int available; } BarnixMouse;
/* ABI 8 snapshot. Wheel counters are cumulative, not consumed by readers. */
typedef struct { BarnixMouse mouse; int wheel, horizontal; } BarnixPointer;
void mouse_pointer(BarnixPointer *state);
int mouse_source(unsigned int source, int connected, int dx, int dy,
                 unsigned int buttons, int wheel, int horizontal);
int mouse_usb_report(const unsigned char *report, unsigned int length);
int mouse_relative(int dx,int dy,unsigned int buttons);
void mouse_init(void);
void mouse_reset(void);
int mouse_feed(unsigned char byte);
void mouse_get(BarnixMouse *state);
#endif
