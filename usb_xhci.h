#ifndef USB_XHCI_H
#define USB_XHCI_H

/* xHCI discovery is shared by HID and the Mass Storage transport. */
int usb_xhci_controller_count(void);

int usb_xhci_poll(void);
int usb_xhci_storage_present(int index);
unsigned int usb_xhci_storage_sectors(int index);
unsigned int usb_xhci_storage_generation(int index);
int usb_xhci_storage_transfer(int index, unsigned int lba, void *data,
                              unsigned int count, int input);
int usb_xhci_storage_flush(int index);
void usb_xhci_diagnostics(void);
#endif
