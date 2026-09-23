#ifndef ELF_H
#define ELF_H
void elf_cache_recovery(void);
int elf_run(const char *name, int argc, const char *const *argv, int *status);
#endif
