#ifndef KEYBOARD_H
#define KEYBOARD_H
unsigned int getch(void);
#define KEY_UP 0x100U
#define KEY_DOWN 0x101U
#define KEY_LEFT 0x102U
#define KEY_RIGHT 0x103U
#define KEY_HOME 0x104U
#define KEY_END 0x105U
#define KEY_DELETE 0x106U
#define KEY_ESCAPE 0x107U
unsigned int keyboard_feed(unsigned char scancode);
void keyboard_set_layout(int russian);
int keyboard_layout(void);
void keyboard_reset(void);
#endif
