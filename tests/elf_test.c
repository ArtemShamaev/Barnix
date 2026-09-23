#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "elf_format.c"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAILED: %s\n", #x); exit(1); } } while (0)
static unsigned char original[300000], modified[300000];
int main(int argc, char **argv)
{
    FILE *f = fopen("apps/hello.elf", "rb"); CHECK(f);
    unsigned int size = fread(original, 1, sizeof(original), f); fclose(f);
    CHECK(size > 12 * 1024);
    ElfPlan plan;
    CHECK(elf_validate(original, size, &plan) == NULL);
    CHECK(plan.count >= 2);
    CHECK(elf_validate(original, size, NULL) != NULL);
    CHECK(elf_validate(original, 0, &plan) != NULL);
    CHECK(elf_validate(original, 51, &plan) != NULL);
#define RESET() memcpy(modified, original, size)
#define REJECT() CHECK(elf_validate(modified, size, &plan) != NULL)
    RESET(); Header *h = (Header *)modified;
    h->ident[0] = 0; REJECT();
    RESET(); h->ident[4] = 2; REJECT();
    RESET(); h->ident[5] = 2; REJECT();
    RESET(); h->type = 3; REJECT();
    RESET(); h->machine = 62; REJECT();
    RESET(); h->phoff = 0xfffffff0; REJECT();
    RESET(); h->phnum = 65535; REJECT();
    RESET(); h->phentsize = 1; REJECT();
    RESET(); h->entry = APP_STACK_TOP; REJECT();
    RESET(); ProgramHeader *p = (ProgramHeader *)(modified + h->phoff);
    CHECK(p[0].type == 1 && p[1].type == 1);
    p[0].filesz = p[0].memsz + 1; REJECT();
    RESET(); p[0].offset = size; p[0].filesz = 1; REJECT();
    RESET(); p[0].vaddr = 0x100000; REJECT();
    RESET(); p[0].memsz = 0xffffffff; REJECT();
    RESET(); p[0].align = 3; REJECT();
    RESET(); p[0].vaddr++; REJECT();
    RESET(); p[1].vaddr = p[0].vaddr; REJECT();
    RESET(); p[1].type = 3; REJECT();
    RESET(); p[1].type = 2; REJECT();
    RESET(); p[1].type = 7; REJECT();
    RESET(); p[0].flags &= ~1U; REJECT();
    RESET(); p[0].filesz = 0; REJECT();
    RESET();
    h->ident[7] = 0; h->ident[8] = 0;
    h->entry += 0x07000000U;
    for (unsigned int i = 0; i < h->phnum; i++)
        if (p[i].type == 1) p[i].vaddr += 0x07000000U;
    CHECK(elf_validate(modified, size, &plan) == NULL);
    CHECK(!plan.legacy && plan.base == 0x08000000U && plan.stack_top == 0x08400000U);
    /* Kernel aliases, cross-window images, and foreign ABI tags are rejected. */
    RESET(); h->ident[7] = 0; h->ident[8] = 0;
    p[0].vaddr = 0x01800000U; h->entry = p[0].vaddr; REJECT();
    RESET(); h->ident[7] = 0; h->ident[8] = 0;
    p[1].vaddr = 0xbffff000U; REJECT();
    RESET(); h->ident[7] = 9; REJECT();
    RESET(); h->ident[8] = 99; REJECT();
    RESET(); CHECK(elf_validate(modified, size, &plan) == NULL && plan.legacy);
    for (int i = 1; i < argc; i++) {
        f = fopen(argv[i], "rb"); CHECK(f);
        size = fread(original, 1, sizeof(original), f); fclose(f);
        CHECK(elf_validate(original, size, &plan) == NULL);
    }
    puts("ELF validation tests passed");
    return 0;
}
