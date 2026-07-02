#include "barnix.h"
#include "fs.h"
#include "disk.h"
extern unsigned int _binary_fs_img_start;
extern unsigned int _binary_fs_img_end;
__attribute__((section(".multiboot"), used))
const unsigned int multiboot_header[] = {
    0x1BADB002,
    0,
    -(0x1BADB002)
};
void bssh_main(void);


void kernel_main(void)
{
    clear();

    println(CYAN, "Barnix OS starting...");

    /* 1. disk first */
    if (disk_init() != 0)
    {
        println(RED, "disk init failed");
        while (1);
    }

    println(GREEN, "disk ready");

    /* 2. fs after disk */
    if (fs_init() != 0)
    {
        println(RED, "fs init failed");
        while (1);
    }

    println(GREEN, "fs ready");

    println(YELLOW, "starting shell...");

    bssh_main();

    while (1)
        __asm__ volatile("hlt");
}
