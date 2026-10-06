#ifndef BARNINO_BDK_API_H
#define BARNINO_BDK_API_H
#include <stddef.h>
#ifdef BARNIX_IN_TREE
#include "../../app_abi.h"
#include "../../keyboard.h"
#else
#include <barnix/app_abi.h>
#include <barnix/keyboard.h>
#endif
#ifdef __cplusplus
extern "C" {
#endif
extern const BarnixAPI *barnix;
extern int barnix_program_argc;
extern const char *const *barnix_program_argv;

/* All calls require the Barnix entry point supplied by BDK. */
static inline void barnix_console_write(const char *text) { barnix->print(15, text); }
static inline void barnix_console_write_line(const char *text) { barnix->println(15, text); }
static inline void barnix_console_clear(void) { barnix->clear(); }
static inline void barnix_console_color(int color, const char *text) { barnix->print(color, text); }
static inline void barnix_console_position(int x, int y) { barnix->goto_xy(x, y); }
static inline unsigned int barnix_console_read_key(void) { return barnix->getch(); }
int barnix_console_read_line(char *buffer, unsigned int capacity);
void barnix_console_write_int(int value);
/* Poll once per loop, handle the key, then read the mouse snapshot.
 * KEY_MOUSE is an input notification, not a character. */
static inline unsigned int barnix_input_poll(void) { return barnix->poll_key(); }
static inline BarnixMouse barnix_mouse_get_state(void) {
    BarnixMouse state = {0,0,0,0,0}; barnix->mouse(&state); return state;
}
static inline int barnix_mouse_is_down(BarnixMouse state, unsigned int button) {
    return (state.buttons & button) != 0;
}
static inline BarnixPointer barnix_pointer_get_state(void) {
    BarnixPointer state; barnix->pointer(&state); return state;
}
static inline void barnix_sleep(unsigned int milliseconds) { barnix->sleep_ms(milliseconds); }
static inline void barnix_screen_show(const unsigned short *cells) { barnix->screen(cells); }
static inline int barnix_file_size(const char *path) { return barnix->size(path); }
static inline int barnix_file_read(const char *path, unsigned int offset, void *data, unsigned int size) {
    return barnix->read(path, offset, data, size);
}
static inline int barnix_file_write(const char *path, const void *data, int size) {
    return barnix->write(path, (const char *)data, size);
}
static inline int barnix_file_append(const char *path, const void *data, int size) {
    return barnix->append(path, (const char *)data, size);
}
static inline const char *barnix_current_user(void) { return barnix->current_user(); }
#ifdef __cplusplus
}
#endif
#endif
