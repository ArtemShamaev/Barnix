#ifndef BARNIX_USB_HID_H
#define BARNIX_USB_HID_H
#define HID_MOUSE_FIELDS 64
typedef struct { unsigned short bit; unsigned char size,id,kind,relative; int minimum,maximum; } HidMouseField;
typedef struct { HidMouseField fields[HID_MOUSE_FIELDS]; unsigned int count,buttons; int report_ids; } HidMouse;
int hid_mouse_descriptor(HidMouse *mouse,const unsigned char *descriptor,unsigned int length);
int hid_mouse_report(HidMouse *mouse,unsigned int source,const unsigned char *report,unsigned int length);
#endif
