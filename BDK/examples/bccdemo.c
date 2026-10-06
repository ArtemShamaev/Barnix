#define BARNIX_APP_NAME "bccdemo.elf"
#include <barnix_graphics.h>

/* Compiled by BCC: no libc, heap, C++ or host dependencies. */
static BglApp app;
static const char *const facts[] = {
    "BCC compiled this Barnix ELF",
    "BGL framebuffer and text fallback",
    "ABI buttons, lists and keyboard",
    "Freestanding C with no host libc"
};

int main(void) {
    bgl_init(&app, "BCC / Barnino C Compiler", 64, 48, 512, 304);
    bgl_list(&app, 1, 16, 40, 480, 144, facts, 4);
    bgl_button(&app, 2, 16, 208, 152, "About BCC");
    bgl_button(&app, 3, 336, 208, 160, "Exit");
    for (;;) {
        BglEvent event = bgl_poll(&app);
        if (event.type == BGL_EVENT_CLOSE ||
            (event.type == BGL_EVENT_CLICK && event.id == 3)) break;
        if (event.type == BGL_EVENT_CLICK && event.id == 2)
            app.title = "BCC demo - native Barnix C";
        bgl_draw(&app);
        barnix_sleep(10);
    }
    bgl_close(&app);
    return 0;
}
