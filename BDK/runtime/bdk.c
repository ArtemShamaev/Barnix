#include <barnix_api.h>
#include "text.h"
void barnix_console_write_int(int value) {
    char text[12]; unsigned int n = value < 0 ? 0U - (unsigned int)value : (unsigned int)value;
    int end = sizeof(text) - 1, i = end; text[i] = 0;
    do { text[--i] = '0' + n % 10; n /= 10; } while (n);
    if (value < 0) text[--i] = '-';
    barnix_console_write(text + i);
}
int barnix_console_read_line(char *buffer, unsigned int capacity) {
    if (!buffer || !capacity) return -1;
    unsigned int length = 0; buffer[0] = 0;
    for (;;) {
        unsigned int key = barnix_console_read_key();
        if (key == '\n' || key == '\r') { barnix_console_write("\n"); return (int)length; }
        if (key == KEY_ESCAPE) return -1;
        if (key == '\b') {
            if (length) {
                do { length--; } while (length && ((unsigned char)buffer[length] & 0xc0) == 0x80);
                buffer[length] = 0; barnix_console_write("\b \b");
            }
            continue;
        }
        if (key < 32 || (key >= KEY_UP && key <= KEY_MOUSE)) continue;
        char encoded[5]; unsigned int count = utf8_encode(key, encoded);
        if (count >= capacity - length) continue;
        for (unsigned int i = 0; i < count; i++) buffer[length++] = encoded[i];
        buffer[length] = 0; encoded[count] = 0; barnix_console_write(encoded);
    }
}
