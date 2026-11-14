#ifndef BARNIX_USB_LEGACY_H
#define BARNIX_USB_LEGACY_H
/* EHCI/OHCI root ports exposed to the shared USB enumeration/class layer.
 * Status bits follow UHCI (connect, change, enable, low speed). */
int usb_legacy_init(void);
unsigned int usb_legacy_status(int port);
void usb_legacy_ack(int port);
int usb_legacy_reset(int port);
void usb_legacy_disable(int port);
/* route: where the device sits. USB_ROUTE_ROOT for a device on the root
 * port itself; otherwise its speed and the hub address and port in front of
 * it (EHCI reaches full/low-speed devices behind a high-speed hub with split
 * transactions through the hub's transaction translator). */
#define USB_SPEED_FULL 0
#define USB_SPEED_LOW 1
#define USB_SPEED_HIGH 2
#define USB_ROUTE_ROOT 3U
#define USB_ROUTE(speed, hub, port) ((unsigned int)(speed) | (unsigned int)(hub) << 8 | (unsigned int)(port) << 16)
int usb_legacy_transfer(int port, unsigned int address, unsigned int endpoint,
                        unsigned int packet, unsigned int pid,
                        unsigned char *toggle, void *data, unsigned int length,
                        unsigned int route);
/* Interrupt IN pipes for mice: open one per mouse on a root port (several
 * may share a port behind a hub). usb_legacy_mouse returns the bytes of a
 * report, -2 when none arrived yet, -3 after a transfer error (the status
 * says which; the next call restarts the pipe), -1 when the controller is
 * gone. */
int usb_legacy_pipe(int port);
void usb_legacy_pipe_close(int pipe);
unsigned int usb_legacy_pipe_status(int pipe);
int usb_legacy_mouse(int pipe, unsigned int address, unsigned int endpoint,
                     unsigned int packet, unsigned char *toggle, void *data,
                     unsigned int route);
/* 1 for an EHCI root port, 0 for OHCI, -1 when absent. */
int usb_legacy_is_ehci(int port);
void usb_legacy_diagnostics(void);
#endif
