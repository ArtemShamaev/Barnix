#ifndef ELF_H
#define ELF_H
void elf_cache_recovery(void);
/* A program run through ABI 13 run() is about to read the keyboard. */
void elf_nested_input(void);
int elf_run(const char *name, int argc, const char *const *argv, int *status);
#endif
