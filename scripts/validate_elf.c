/* Use the kernel's loader rules when importing an application offline. */
#include <stdio.h>
#include <stdlib.h>
#include "elf_format.h"
#include "fs.h"
int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    FILE *file = fopen(argv[1], "rb");
    if (!file) { perror(argv[1]); return 1; }
    unsigned char *data = malloc(FS_MAX_FILE_SIZE + 1);
    if (!data) { fclose(file); return 1; }
    size_t size = fread(data, 1, FS_MAX_FILE_SIZE + 1, file);
    int failed = ferror(file);
    fclose(file);
    ElfPlan plan;
    const char *error = failed ? "cannot read ELF" : size > FS_MAX_FILE_SIZE
        ? "ELF exceeds Barnix file size limit" : elf_validate(data, size, &plan);
    free(data);
    if (error) { fprintf(stderr, "%s: %s\n", argv[1], error); return 1; }
    return 0;
}
