#ifndef BARNIX_USB_LEGACY_H
#define BARNIX_USB_LEGACY_H
/* EHCI/OHCI root ports exposed to the shared USB enumeration/class layer.
 * Status bits follow UHCI (connect, change, enable, low speed). */
int usb_legacy_init(void);
unsigned int usb_legacy_status(int port);
void usb_legacy_ack(int port);
int usb_legacy_reset(int port);
void usb_legacy_disable(int port);
int usb_legacy_transfer(int port, unsigned int address, unsigned int endpoint,
                        unsigned int packet, unsigned int pid,
                        unsigned char *toggle, void *data, unsigned int length);
int usb_legacy_mouse(int port, unsigned int address, unsigned int endpoint,
                     unsigned int packet, unsigned char *toggle, void *data);
void usb_legacy_mouse_stop(int port);
void usb_legacy_diagnostics(void);
#endif
