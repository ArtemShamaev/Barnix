#ifndef COMMAND_H
#define COMMAND_H
#include "barnix_app.h"
#include <stddef.h>
size_t strlen(const char *s);
static inline int fail(const char *message) { barnix->println(4, barnix->translate(message)); return 1; }
static inline int result(int code) {
    return code ? fail("operation failed (check name, type, space or disk)") : 0;
}
#endif
