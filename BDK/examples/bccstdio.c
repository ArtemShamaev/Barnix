#define BARNIX_APP_NAME "bccstdio.elf"
#include <stdio.h>

int main(void) {
    char line[64];
    int value = 42;
    int length = snprintf(line, sizeof(line), "BCC stdio: value=%d", value);
    printf("%s (length %d)\n", line, length);
    puts("printf, puts and snprintf run through Barnix ABI");
    return length == 19 ? 0 : 1;
}
