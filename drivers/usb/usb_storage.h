#ifndef USB_STORAGE_H
#define USB_STORAGE_H

/* UHCI/OHCI/EHCI root ports and xHCI BOT devices share the disk namespace. */
#define USB_STORAGE_MAX 48
/* Shared legacy-controller HID input; nonblocking once enumerated. */
int usb_mouse_poll(void);
int usb_storage_select(int index);
int usb_storage_probe(void);
int usb_storage_present(void);
unsigned int usb_storage_sectors(void);
unsigned int usb_storage_generation(void);
int usb_storage_read(unsigned int lba, void *buffer, unsigned int count);
int usb_storage_write(unsigned int lba, const void *buffer, unsigned int count);
int usb_storage_flush(void);

void usb_storage_diagnostics(void);
#endif
