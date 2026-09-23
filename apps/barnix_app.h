#ifndef BARNIX_APP_H
#define BARNIX_APP_H
#include "../app_abi.h"
extern const BarnixAPI *barnix;
/* The CRT calls this entry with argv[0] set to the executable name. */
int main(int argc, const char *const *argv);
#endif
