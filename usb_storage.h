#ifndef USB_STORAGE_H
#define USB_STORAGE_H

/* One full-speed USB mass-storage device on a directly attached UHCI port. */
int usb_storage_probe(void);
int usb_storage_present(void);
unsigned int usb_storage_sectors(void);
unsigned int usb_storage_generation(void);
int usb_storage_read(unsigned int lba, void *buffer, unsigned int count);
int usb_storage_write(unsigned int lba, const void *buffer, unsigned int count);
int usb_storage_flush(void);

#endif
