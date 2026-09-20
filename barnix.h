#ifndef BARNIX_H
#define BARNIX_H

#include <stddef.h>

#define VGA_ADDRESS 0xB8000
#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define MAKE_ATTR(color) ((color) & 0x0F)

enum {
    BLACK, BLUE, GREEN, CYAN, RED, MAGENTA, BROWN, LIGHT_GREY,
    DARK_GREY, LIGHT_BLUE, LIGHT_GREEN, LIGHT_CYAN, LIGHT_RED,
    LIGHT_MAGENTA, YELLOW, WHITE
};

void clear(void);
void print(int color, const char *str);
void println(int color, const char *str);
void printll(int color, const char *str);
int strlen(const char *str);
int strcmp(const char *str1, const char *str2);
void strcpy(char *dest, const char *src);
void strcat(char *dest, const char *src);
char* strchr(const char *s, int c);
char* strstr(const char *haystack, const char *needle);
void input(char *buffer, int max_length, const char *prompt);
int strncmp(const char *a, const char *b, int n);
void strncpy(char *dst, const char *src, int n);
int strnlen(const char *str, int n);
void *memcpy(void *dst, const void *src, int size);
void *memmove(void *dst, const void *src, int size);
void *memset(void *dst, int value, int size);
int memcmp(const void *a, const void *b, int size);
void itoa(int value, char *buffer);
void utoa(unsigned int value, char *buffer);
void xtoa(unsigned int value, char *buffer);
int atoi(const char *str);
void print_int(int color, int value);
void print_uint(int color, unsigned int value);
void print_hex(int color, unsigned int value);
int isdigit(char c);
int isalpha(char c);
int isspace(char c);
char toupper(char c);
char tolower(char c);
int starts_with(const char *str, const char *prefix);
int ends_with(const char *str, const char *suffix);
char *strtok(char *str, const char *delim);
void banner(const char *title);
void panic(const char *msg);

#endif
