#include "command.h"
int main(int argc,const char *const *argv) {
    if(argc!=2)return fail("theme default|dark|light|solarized|monokai|dracula|nord|bios|pink-panther|green|random");
    if(barnix->theme(argv[1]))return fail("Unknown theme or cannot save user settings");
    barnix->puts("Theme applied and saved");return 0;
}
