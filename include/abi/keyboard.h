#ifndef KEYBOARD_H
#define KEYBOARD_H
unsigned int getch(void);
unsigned int keyboard_event(void);
unsigned int keyboard_poll(void);
void keyboard_sleep(unsigned int milliseconds);
#define KEY_UP 0x100U
#define KEY_DOWN 0x101U
#define KEY_LEFT 0x102U
#define KEY_RIGHT 0x103U
#define KEY_HOME 0x104U
#define KEY_END 0x105U
#define KEY_DELETE 0x106U
#define KEY_ESCAPE 0x107U
#define KEY_MOUSE 0x108U
unsigned int keyboard_feed(unsigned char scancode);
/* ABI 10 raw key events: code is the set-1 make code, 0x80|code for E0 keys.
 * key is the character or KEY_* value poll_key reports for a press, else 0. */
typedef struct { unsigned short code; unsigned char pressed, repeat; unsigned int key; } BarnixKeyEvent;
int keyboard_key_event(BarnixKeyEvent *event);
void keyboard_events_reset(void);
void keyboard_set_layout(int russian);
int keyboard_layout(void);
/* Ctrl+Alt+M: the arrows move the pointer, Space/Enter click (Shift: right). */
int keyboard_mouse_enabled(void);
void keyboard_reset(void);
#endif
