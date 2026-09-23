#include "command.h"
int main(int argc, const char *const *argv)
{
    (void)argv; if (argc != 1) return fail("usage: devices");
    barnix->devices();
    if (barnix->net_state() < 0) barnix->puts("network: no supported adapter");
    else if (barnix->net_state() == 2) barnix->puts("network: connected");
    else barnix->puts("network: adapter detected, disconnected");
    return 0;
}
