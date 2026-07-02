#ifndef BARNIX_H
#define BARNIX_H

#define VGA_ADDRESS 0xB8000
#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define MAKE_ATTR(color) ((color) & 0x0F)

#define BLACK 0
#define BLUE 1
#define GREEN 2
#define CYAN 3
#define RED 4
#define MAGENTA 5
#define BROWN 6
#define LIGHT_GREY 7
#define DARK_GREY 8
#define LIGHT_BLUE 9
#define LIGHT_GREEN 10
#define LIGHT_CYAN 11
#define LIGHT_RED 12
#define LIGHT_MAGENTA 13
#define YELLOW 14
#define WHITE 15

#ifndef NULL
#define NULL ((void *)0)
#endif

void clear(void);
void print(int color, const char *str);
void println(int color, const char *str);
void printll(int color, const char *str);
void input(char *buffer, int max_length, const char *prompt);

int strlen(const char *str);
int strcmp(const char *str1, const char *str2);
void strcpy(char *dest, const char *src);
void strcat(char *dest, const char *src);
char *strchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);
int strncmp(const char *a, const char *b, int n);
void strncpy(char *dst, const char *src, int n);
int strnlen(const char *str, int n);
char *strtok(char *str, const char *delim);
void *memcpy(void *dst, const void *src, int size);
void *memmove(void *dst, const void *src, int size);
void *memset(void *dst, int value, int size);
int memcmp(const void *a, const void *b, int size);
void print_int(int color, int value);
void print_uint(int color, unsigned int value);
void print_hex(int color, unsigned int value);
void banner(const char *title);
void panic(const char *msg);

#endif
