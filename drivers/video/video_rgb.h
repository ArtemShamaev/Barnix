#ifndef VIDEO_RGB_H
#define VIDEO_RGB_H
/* Legacy BIOS boots: enable VGA/Bochs output for video_present. */
void video_legacy_init(void);
/* XRGB8888 frame scaled to the display; NULL restores the text console. */
int video_present(const unsigned int *pixels, unsigned int width, unsigned int height);
int video_display(unsigned int *width, unsigned int *height);
void video_leave(void);
#endif
