#include <barnix_api.h>
const BarnixAPI *barnix;
int barnix_program_argc;
const char *const *barnix_program_argv;
extern int main(int argc, const char *const *argv);
typedef void (*Initializer)(void);
/* Clang uses atexit even with -fno-use-cxa-atexit; GCC also uses it for
 * local static destructors. No allocator or host C runtime is required. */
static Initializer exit_handlers[64];
static unsigned int exit_count;
int atexit(Initializer function) {
    if (!function || exit_count == 64) return -1;
    exit_handlers[exit_count++] = function;
    return 0;
}
extern Initializer __init_array_start[], __init_array_end[];
extern Initializer __fini_array_start[], __fini_array_end[];
int _start(const BarnixAPI *api, int argc, const char *const *argv) {
    if (!api || api->version != BARNIX_APP_ABI) return 126;
    barnix = api; barnix_program_argc = argc; barnix_program_argv = argv;
    for (Initializer *fn = __init_array_start; fn != __init_array_end; ++fn) (*fn)();
    int result = main(argc, argv);
    while (exit_count) exit_handlers[--exit_count]();
    for (Initializer *fn = __fini_array_end; fn != __fini_array_start;) (*--fn)();
    return result;
}
