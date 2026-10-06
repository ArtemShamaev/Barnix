#define BARNIX_APP_NAME "app.elf"
#include <barnix_api.h>
#include <stdio.h>

int main(void) {
    barnix_console_clear();
    barnix_console_write_line("BDK mouse demo: move/click; Q or Escape exits.");
    for (;;) {
        unsigned int key = barnix_input_poll();
        if (key == 'q' || key == 'Q' || key == KEY_ESCAPE) break;
        BarnixMouse mouse = barnix_mouse_get_state();
        barnix_console_position(0, 2);
        if (mouse.available) {
            printf("X: %d Y: %d Buttons: %d       ", mouse.x, mouse.y, (int)mouse.buttons);
            barnix_console_position(0, 3);
            barnix_console_write(barnix_mouse_is_down(mouse, BARNIX_MOUSE_LEFT) ? "Left pressed    " : "Left released   ");
        } else barnix_console_write("No mouse detected. Q exits.");
        barnix_sleep(10);
    }
    barnix_console_clear();
    return 0;
}
