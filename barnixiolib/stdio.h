#ifndef BARNIXIOLIB_STDIO_H
#define BARNIXIOLIB_STDIO_H

#ifdef BARNIX_BDK
#include <barnix/app_abi.h>
#else
#include "../app_abi.h"
#endif
#include <stddef.h>
#include <stdarg.h>

#ifndef EOF
#define EOF (-1)
#endif
#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

/* A deliberately small stdio-shaped interface for Barnix ELF programs.
 * There is no host libc: the implementation forwards to the Barnix ABI. */
typedef struct BarnixFILE { const char *name; unsigned int position; int append; } FILE;

int putchar(int character);
int puts(const char *text);
int getchar(void);
int printf(const char *format, ...);
int fprintf(FILE *stream, const char *format, ...);
int vprintf(const char *format, va_list arguments);
int vsnprintf(char *buffer, size_t capacity, const char *format, va_list arguments);
int snprintf(char *buffer, size_t capacity, const char *format, ...);
int fputc(int character, FILE *stream);
int fputs(const char *text, FILE *stream);
int fgetc(FILE *stream);
int fflush(FILE *stream);
int feof(FILE *stream);
int ferror(FILE *stream);
int scanf(const char *format, ...);
int argument_count(void);
const char *argument_value(int index);
int argument_int(int index, int *value);
FILE *fopen(const char *name, const char *mode);
int fclose(FILE *stream);
int fseek(FILE *stream, long offset, int origin);
long ftell(FILE *stream);
int remove(const char *name);
int rename(const char *old_name, const char *new_name);
unsigned int barnix_fread(void *data, unsigned int size, unsigned int count, FILE *stream);
unsigned int barnix_fwrite(const void *data, unsigned int size, unsigned int count, FILE *stream);
#define fread barnix_fread
#define fwrite barnix_fwrite

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

#endif
