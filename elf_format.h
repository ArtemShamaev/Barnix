#ifndef ELF_FORMAT_H
#define ELF_FORMAT_H
#include <stdint.h>
#include "app_abi.h"

#define ELF_MAX_SEGMENTS 32

typedef struct {
    uint32_t offset, address, file_size, memory_size;
} ElfSegment;
typedef struct {
    uint32_t entry, count;
    uint32_t base, stack_top, phoff, phnum, phdr, legacy;
    ElfSegment segments[ELF_MAX_SEGMENTS];
} ElfPlan;

/* Returns zero on success, otherwise a diagnostic string. No memory writes
 * outside plan occur until all ELF headers and load ranges are validated. */
const char *elf_validate(const void *data, unsigned int size, ElfPlan *plan);
#endif
