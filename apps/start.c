#include "barnix_app.h"
const BarnixAPI *barnix;
int barnix_program_argc;
const char *const *barnix_program_argv;
int _start(const BarnixAPI *api, int argc, const char *const *argv)
{
    if (!api || api->version != BARNIX_APP_ABI) return 126;
    barnix = api;
    barnix_program_argc = argc;
    barnix_program_argv = argv;
    return main(argc, argv);
}
