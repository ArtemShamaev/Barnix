#ifndef BARNIXIOLIB_STDIO_H
#define BARNIXIOLIB_STDIO_H

#include "../app_abi.h"

/* A deliberately small stdio-shaped interface for Barnix ELF programs.
 * There is no host libc: the implementation forwards to the Barnix ABI. */
typedef struct BarnixFILE { const char *name; unsigned int position; int append; } FILE;

int putchar(int character);
int puts(const char *text);
int getchar(void);
int printf(const char *format, ...);
int scanf(const char *format, ...);
int argument_count(void);
const char *argument_value(int index);
int argument_int(int index, int *value);
FILE *fopen(const char *name, const char *mode);
int fclose(FILE *stream);
unsigned int barnix_fread(void *data, unsigned int size, unsigned int count, FILE *stream);
unsigned int barnix_fwrite(const void *data, unsigned int size, unsigned int count, FILE *stream);
#define fread barnix_fread
#define fwrite barnix_fwrite

#endif
