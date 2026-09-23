#include "barnix.h"
#include "keyboard.h"
#include "text.h"
/* =========================================================
   Barnix I/O Library Implementation
   Copyright (C) Barnino Systems, all rights reserved.
   ========================================================= */


/* Указатель на видеопамять */
static unsigned short *vga_buffer = (unsigned short *)VGA_ADDRESS;

/* Текущая позиция курсора */
static int cursor_x = 0;
static int cursor_y = 0;
static char input_history[16][128];
static unsigned int input_history_count;

/* ==================== ВНУТРЕННИЕ ФУНКЦИИ ==================== */

/* Установка курсора */
void set_cursor(int x, int y) {
    cursor_x = x;
    cursor_y = y;
    unsigned short pos = y * VGA_WIDTH + x;
    
    __asm__ volatile (
        "outb %%al, %%dx\n"
        : : "a" (0x0E), "d" (0x3D4)
    );
    __asm__ volatile (
        "outb %%al, %%dx\n"
        : : "a" ((pos >> 8) & 0xFF), "d" (0x3D5)
    );
    __asm__ volatile (
        "outb %%al, %%dx\n"
        : : "a" (0x0F), "d" (0x3D4)
    );
    __asm__ volatile (
        "outb %%al, %%dx\n"
        : : "a" (pos & 0xFF), "d" (0x3D5)
    );
}

/* Вывод символа */
static void putchar(unsigned char c, int color) {
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\r') {
        cursor_x = 0;
    } else {
        unsigned short attribute = (MAKE_ATTR(color) << 8);
        vga_buffer[cursor_y * VGA_WIDTH + cursor_x] = attribute | c;
        cursor_x++;
    }
    
    if (cursor_x >= VGA_WIDTH) {
        cursor_x = 0;
        cursor_y++;
    }
    
    if (cursor_y >= VGA_HEIGHT) {
        /* Прокрутка экрана */
        for (int i = 0; i < (VGA_HEIGHT - 1) * VGA_WIDTH; i++) {
            vga_buffer[i] = vga_buffer[i + VGA_WIDTH];
        }
        for (int i = (VGA_HEIGHT - 1) * VGA_WIDTH; i < VGA_HEIGHT * VGA_WIDTH; i++) {
            vga_buffer[i] = (MAKE_ATTR(WHITE) << 8) | ' ';
        }
        cursor_y = VGA_HEIGHT - 1;
    }
    
    set_cursor(cursor_x, cursor_y);
}

/* ==================== ОСНОВНЫЕ ФУНКЦИИ ==================== */

/* Очистка экрана */
void clear(void) {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga_buffer[i] = (MAKE_ATTR(WHITE) << 8) | ' ';
    }
    cursor_x = 0;
    cursor_y = 0;
    set_cursor(0, 0);
}

/* Вывод строки без переноса */
static Utf8Decoder output_decoder;
void print(int color, const char *str) {
    while (*str) {
        unsigned int codes[2];
        int n = utf8_feed(&output_decoder, (unsigned char)*str++, codes);
        for (int i = 0; i < n; i++) putchar(text_glyph(codes[i]), color);
    }
}

/* Keep shell status messages separate without changing program output bytes. */
void console_finish_line(void)
{
    if (cursor_x) print(WHITE, "\n");
}

/* Вывод строки с переносом */
void println(int color, const char *str) {
    print(color, str);
    print(color, "\n");
}

/* Вывод с [OK] в начале (зеленый) */
void printll(int color, const char *str) {
    print(GREEN, "[OK] ");
    println(color, str);
}

/* ==================== СТРОКОВЫЕ ФУНКЦИИ ==================== */

/* Длина строки */
int strlen(const char *str) {
    int len = 0;
    while (str[len]) len++;
    return len;
}

/* Сравнение строк */
int strcmp(const char *str1, const char *str2) {
    while (*str1 && *str2 && *str1 == *str2) {
        str1++;
        str2++;
    }
    return *str1 - *str2;
}

/* Копирование строки */
void strcpy(char *dest, const char *src) {
    while (*src) {
        *dest = *src;
        dest++;
        src++;
    }
    *dest = '\0';
}

/* Конкатенация строк */
void strcat(char *dest, const char *src) {
    while (*dest) dest++;
    while (*src) {
        *dest = *src;
        dest++;
        src++;
    }
    *dest = '\0';
}

/* Поиск символа в строке */
char* strchr(const char *s, int c) {
    while (*s) {
        if (*s == c) return (char*)s;
        s++;
    }
    if (c == '\0') return (char*)s;
    return NULL;
}

/* Поиск подстроки */
char* strstr(const char *haystack, const char *needle) {
    if (*needle == '\0') return (char*)haystack;
    for (; *haystack; haystack++) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && *h == *n) {
            h++;
            n++;
        }
        if (*n == '\0') return (char*)haystack;
    }
    return NULL;
}

/* ==================== ВВОД ==================== */

/* Ввод строки с приглашением */
void input(char *buffer, int max_length, const char *prompt) {
    print(WHITE, prompt);
    int start_x = cursor_x, start_y = cursor_y;
    int pos = 0;
    int history_position = (int)input_history_count;
    /* Очищаем буфер */
    for (int i = 0; i < max_length; i++) {
        buffer[i] = '\0';
    }
    
    while (1) {
        unsigned int ch = getch();

        if (ch == KEY_UP || ch == KEY_DOWN) {
            if (ch == KEY_UP && history_position > 0) history_position--;
            else if (ch == KEY_DOWN && history_position < (int)input_history_count) history_position++;
            else continue;
            if (history_position < (int)input_history_count) strcpy(buffer, input_history[history_position]);
            else buffer[0] = 0;
            pos = strlen(buffer);
            for (int y = start_y; y < start_y + 3 && y < VGA_HEIGHT; y++)
                for (int x = y == start_y ? start_x : 0; x < VGA_WIDTH; x++)
                    vga_buffer[y * VGA_WIDTH + x] = (MAKE_ATTR(WHITE) << 8) | ' ';
            set_cursor(start_x, start_y); print(WHITE, buffer);
            continue;
        }
        if (ch == '\t' && pos > 0) {
            int end = pos, begin = 0;
            while (begin < end && buffer[begin] == ' ') begin++;
            while (begin < end && buffer[begin] != ' ') begin++;
            if (begin == end) {
                static const char *commands[] = {
                    "ls","pwd","df","diskinfo","devices","clear","touch","rm",
                    "mkdir","cd","rmdir","stat","cp","mv","write","append","cat",
                    "echo","panic","sync","mount","unmount","help","init","macro",
                    "bnm","get","git","true","false","linux"
                };
                int match = -1, matches = 0;
                for (unsigned int i = 0; i < sizeof(commands) / sizeof(commands[0]); i++)
                    if (!strncmp(commands[i], buffer, pos)) { match = (int)i; matches++; }
                if (matches == 1) { strcpy(buffer, commands[match]); pos = strlen(buffer); }
                for (int y = start_y; y < start_y + 3 && y < VGA_HEIGHT; y++)
                    for (int x = y == start_y ? start_x : 0; x < VGA_WIDTH; x++)
                        vga_buffer[y * VGA_WIDTH + x] = (MAKE_ATTR(WHITE) << 8) | ' ';
                set_cursor(start_x, start_y); print(WHITE, buffer);
            }
            continue;
        }
        
        /* Enter */
        if (ch == '\n') {
            buffer[pos] = '\0';
            putchar('\n', WHITE);
            if (pos && (!input_history_count || strcmp(input_history[input_history_count - 1], buffer))) {
                if (input_history_count == 16) {
                    for (int i = 1; i < 16; i++) strcpy(input_history[i - 1], input_history[i]);
                    input_history_count--;
                }
                strcpy(input_history[input_history_count++], buffer);
            }
            break;
        }
        
        /* Backspace */
        if (ch == 0x08) {
            if (pos > 0) {
                do { pos--; } while (pos > 0 && ((unsigned char)buffer[pos] & 0xc0) == 0x80);
                buffer[pos] = 0;
                if (cursor_x == 0 && cursor_y > 0) { cursor_y--; cursor_x = VGA_WIDTH; }
                if (cursor_x > 0) cursor_x--;
                unsigned short attribute = (MAKE_ATTR(WHITE) << 8);
                vga_buffer[cursor_y * VGA_WIDTH + cursor_x] = attribute | ' ';
                set_cursor(cursor_x, cursor_y);
            }
            continue;
        }
        
        /* Обычный символ */
        if (ch >= 32 && ch != 127) {
            char encoded[4]; unsigned int n = utf8_encode(ch, encoded);
            if (pos + (int)n < max_length) {
                for (unsigned int i = 0; i < n; i++) buffer[pos++] = encoded[i];
                buffer[pos] = 0;
                putchar(text_glyph(ch), WHITE);
            }
        }
    }
}
/* =========================================================
   Дополнительные строковые функции
   ========================================================= */

/* Сравнение первых n символов */
int strncmp(const char *a, const char *b, int n)
{
    while (n > 0)
    {
        if (*a != *b)
            return (unsigned char)*a - (unsigned char)*b;

        if (*a == '\0')
            return 0;

        a++;
        b++;
        n--;
    }

    return 0;
}

/* Копирование первых n символов */
void strncpy(char *dst, const char *src, int n)
{
    while (n > 0 && *src)
    {
        *dst++ = *src++;
        n--;
    }

    while (n > 0)
    {
        *dst++ = '\0';
        n--;
    }
}

/* Длина строки максимум n */
int strnlen(const char *str, int n)
{
    int len = 0;

    while (len < n && str[len])
        len++;

    return len;
}

/* =========================================================
   Работа с памятью
   ========================================================= */

/* Копирование памяти */
void *memcpy(void *dst, const void *src, int size)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;

    while (size--)
        *d++ = *s++;

    return dst;
}

/* Перемещение памяти (поддерживает перекрытие) */
void *memmove(void *dst, const void *src, int size)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;

    if (d == s)
        return dst;

    if (d < s)
    {
        while (size--)
            *d++ = *s++;
    }
    else
    {
        d += size;
        s += size;

        while (size--)
            *--d = *--s;
    }

    return dst;
}

/* Заполнение памяти */
void *memset(void *dst, int value, int size)
{
    unsigned char *d = (unsigned char *)dst;

    while (size--)
        *d++ = (unsigned char)value;

    return dst;
}

/* Сравнение памяти */
int memcmp(const void *a, const void *b, int size)
{
    const unsigned char *x = (const unsigned char *)a;
    const unsigned char *y = (const unsigned char *)b;

    while (size--)
    {
        if (*x != *y)
            return *x - *y;

        x++;
        y++;
    }

    return 0;
}
/* =========================================================
   Number conversion
   ========================================================= */

static void reverse(char *str)
{
    int i = 0;
    int j = strlen(str) - 1;

    while (i < j)
    {
        char t = str[i];
        str[i] = str[j];
        str[j] = t;
        i++;
        j--;
    }
}

/* int -> string */
void itoa(int value, char *buffer)
{
    int neg = 0;
    int i = 0;

    if (value == 0)
    {
        buffer[0] = '0';
        buffer[1] = 0;
        return;
    }

    if (value < 0)
    {
        neg = 1;
        value = -value;
    }

    while (value > 0)
    {
        buffer[i++] = '0' + (value % 10);
        value /= 10;
    }

    if (neg)
        buffer[i++] = '-';

    buffer[i] = 0;

    reverse(buffer);
}

/* unsigned -> string */
void utoa(unsigned int value, char *buffer)
{
    int i = 0;

    if (value == 0)
    {
        buffer[0] = '0';
        buffer[1] = 0;
        return;
    }

    while (value > 0)
    {
        buffer[i++] = '0' + (value % 10);
        value /= 10;
    }

    buffer[i] = 0;

    reverse(buffer);
}

/* hex -> string */
void xtoa(unsigned int value, char *buffer)
{
    const char table[] = "0123456789ABCDEF";
    int i = 0;

    if (value == 0)
    {
        buffer[0] = '0';
        buffer[1] = 0;
        return;
    }

    while (value > 0)
    {
        buffer[i++] = table[value & 0xF];
        value >>= 4;
    }

    buffer[i] = 0;

    reverse(buffer);
}

/* string -> int */
int atoi(const char *str)
{
    int sign = 1;
    int value = 0;

    if (*str == '-')
    {
        sign = -1;
        str++;
    }

    while (*str >= '0' && *str <= '9')
    {
        value = value * 10 + (*str - '0');
        str++;
    }

    return value * sign;
}

/* =========================================================
   Printing numbers
   ========================================================= */

void print_int(int color, int value)
{
    char buf[16];

    itoa(value, buf);

    print(color, buf);
}

void print_uint(int color, unsigned int value)
{
    char buf[16];

    utoa(value, buf);

    print(color, buf);
}

void print_hex(int color, unsigned int value)
{
    char buf[16];

    print(color, "0x");

    xtoa(value, buf);

    print(color, buf);
}
/* =========================================================
   Character functions
   ========================================================= */

int isdigit(char c)
{
    return (c >= '0' && c <= '9');
}

int isalpha(char c)
{
    return ((c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z'));
}

int isspace(char c)
{
    return (c == ' '  ||
            c == '\t' ||
            c == '\n' ||
            c == '\r');
}

char toupper(char c)
{
    if (c >= 'a' && c <= 'z')
        return c - 32;

    return c;
}

char tolower(char c)
{
    if (c >= 'A' && c <= 'Z')
        return c + 32;

    return c;
}

/* =========================================================
   String helpers
   ========================================================= */

int starts_with(const char *str, const char *prefix)
{
    while (*prefix)
    {
        if (*str != *prefix)
            return 0;

        str++;
        prefix++;
    }

    return 1;
}

int ends_with(const char *str, const char *suffix)
{
    int len1 = strlen(str);
    int len2 = strlen(suffix);

    if (len2 > len1)
        return 0;

    return strcmp(str + len1 - len2, suffix) == 0;
}

/* =========================================================
   strtok
   ========================================================= */

char *strtok(char *str, const char *delim)
{
    static char *last;

    if (str)
        last = str;

    if (!last)
        return NULL;

    while (*last && strchr(delim, *last))
        last++;

    if (*last == 0)
    {
        last = NULL;
        return NULL;
    }

    char *start = last;

    while (*last && !strchr(delim, *last))
        last++;

    if (*last)
    {
        *last = 0;
        last++;
    }
    else
    {
        last = NULL;
    }

    return start;
}

/* =========================================================
   Banner
   ========================================================= */

void banner(const char *title)
{
    println(CYAN,
    "========================================");

    print(YELLOW, " ");

    println(WHITE, title);

    println(CYAN,
    "========================================");
}

/* =========================================================
   Panic
   ========================================================= */

void panic(const char *msg)
{
    clear();

    banner("KERNEL PANIC");

    println(RED, msg);
	println(RED, "Reboot your PC, please!");
    println(WHITE, "");

    println(WHITE,
    "System halted.");

    while (1)
    {
        __asm__ volatile("cli");
        __asm__ volatile("hlt");
    }
}
