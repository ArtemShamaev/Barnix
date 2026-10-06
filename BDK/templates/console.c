#define BARNIX_APP_NAME "app.elf"
#include <barnix_api.h>

int main(void) {
    barnix_console_write_line("Hello from Barnino Systems BDK!");
    barnix_console_write("User: ");
    barnix_console_write_line(barnix_current_user());
    return 0;
}
