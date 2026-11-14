#include "barnix.h"
#include "keyboard.h"
#include "text.h"
#include "console_video.h"
#include "video_rgb.h"
/* =========================================================
   Barnix I/O Library Implementation
   Copyright (C) Barnino Systems, all rights reserved.
   ========================================================= */


/* Указатель на видеопамять */
#define vga_buffer console_cells

/* Текущая позиция курсора */
static int cursor_x = 0;
static int cursor_y = 0;
static char input_history[16][128];
static unsigned int input_history_count;
void input_history_clear(void) { memset(input_history,0,sizeof(input_history));input_history_count=0; }

static int theme_background, theme_foreground = WHITE;
static int ansi_attribute = -1;   /* a color chosen by an ANSI sequence, -1: the theme's */
int console_attribute(int color) {
    if (ansi_attribute >= 0 && color == WHITE) return ansi_attribute;
    return (theme_background << 4) | (color == WHITE ? theme_foreground : color & 15);
}
void console_theme(int background, int foreground) {
    theme_background = background & 7; theme_foreground = foreground & 15;
    for (int i=0;i<2000;i++) {
        unsigned int ch=console_cells[i]&255;
        console_cells[i]=(console_attribute(WHITE)<<8)|ch;
    }
    console_present();
}

/* ==================== ВНУТРЕННИЕ ФУНКЦИИ ==================== */

/* Установка курсора */
void set_cursor(int x, int y) {
    console_cursor_visible(1);
    cursor_x = x;
    cursor_y = y;
    if (console_graphics() || console_windowed()) { console_video_cursor(x, y); return; }
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

/* Console output can be collected instead of shown (ABI 13 run: Gouse
 * Prompt shows what a program prints in its window). */
static char *capture_buffer;
static unsigned int capture_capacity, capture_length;
static int capture_echo, capture_paused;
void console_capture_pause(int on) { capture_paused = on; }   /* shown, not collected */
void console_capture(char *buffer, unsigned int capacity)
{
    capture_buffer = capacity ? buffer : 0; capture_capacity = capacity; capture_length = 0; capture_echo = capture_paused = 0;
}
/* Collected but not shown (what was typed, already on the screen). */
void console_capture_add(const char *text)
{
    if (!capture_buffer || capture_paused) return;
    while (*text && capture_length + 1 < capture_capacity) capture_buffer[capture_length++] = *text++;
}
unsigned int console_captured(void) { return capture_length; }
/* Collect and show at once (a program running in a Gouse window). */
void console_capture_echo(int on) { capture_echo = on; }
/* Called after collected output (the window shows a slow program's output). */
static void (*capture_watch)(void);
void console_capture_watch(void (*watch)(void)) { capture_watch = watch; }

/* ABI 14: the system clipboard, one for the shell and every program. */
static char clipboard[65536];
static unsigned int clipboard_length, clipboard_counter;
int clipboard_set(const char *text, unsigned int length)
{
    if (!text) length = 0;
    if (length > sizeof(clipboard) - 1) length = sizeof(clipboard) - 1;
    memcpy(clipboard, text, (int)length);
    clipboard[length] = 0;
    clipboard_length = length;
    clipboard_counter++;
    return 0;
}
int clipboard_get(char *out, unsigned int capacity)
{
    if (out && capacity) {
        unsigned int n = clipboard_length < capacity - 1 ? clipboard_length : capacity - 1;
        memcpy(out, clipboard, (int)n);
        out[n] = 0;
    }
    return (int)clipboard_length;
}
unsigned int clipboard_serial(void) { return clipboard_counter; }

/* Очистка экрана */
void clear(void) {
    if (capture_buffer && !capture_paused) {
        if (capture_length + 1 < capture_capacity) capture_buffer[capture_length++] = '\f';
        if (!capture_echo) return;
    }
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
    if (capture_buffer && !capture_paused) {
        const char *p = str;
        while (*p && capture_length + 1 < capture_capacity) capture_buffer[capture_length++] = *p++;
        if (!capture_echo) { if (capture_watch) capture_watch(); return; }
    }
    while (*str) {
        unsigned int codes[2];
        int n = utf8_feed(&output_decoder, (unsigned char)*str++, codes);
        for (int i = 0; i < n; i++) putchar(text_glyph(codes[i]), color);
    }
}

/* Output of Linux programs: text with the ANSI/VT100 sequences terminal
 * programs use (cursor position, clearing, colors, cursor on/off). A
 * program that moves the cursor is a full-screen one (elf_nested_tui). */
void elf_nested_tui(void);
void console_ansi_write(const char *data, unsigned int length)
{
    static int state, params[8], count, private_mode;
    static int bold, fg = -1, bg = -1, reverse;
    static const unsigned char vga[8] = {0, 4, 2, 6, 1, 5, 3, 7};
    char text[129]; unsigned int used = 0;
#define FLUSH() do { if (used) { text[used] = 0; print(WHITE, text); used = 0; } } while (0)
    for (unsigned int i = 0; i < length; i++) {
        unsigned char c = (unsigned char)data[i];
        if (state == 0) {
            if (c == 27) { FLUSH(); state = 1; continue; }
            text[used++] = (char)c;
            if (used == 128) FLUSH();
            continue;
        }
        if (state == 1) {
            if (c == '[') { state = 2; count = 0; private_mode = 0; for (int k = 0; k < 8; k++) params[k] = -1; }
            else state = 0;   /* ESC x: two-byte sequences are dropped */
            continue;
        }
        /* state 2: CSI parameters, then the final byte */
        if (c == '?') { private_mode = 1; continue; }
        if (c >= '0' && c <= '9') { if (!count) count = 1; if (params[count - 1] < 0) params[count - 1] = 0; params[count - 1] = params[count - 1] * 10 + (c - '0'); continue; }
        if (c == ';') { if (!count) count = 1; if (count < 8) count++; continue; }
        state = 0;
        int a = params[0] < 0 ? 1 : params[0], b = params[1] < 0 ? 1 : params[1];
        switch (c) {
        case 'H': case 'f':
            elf_nested_tui();
            cursor_y = a - 1; cursor_x = b - 1;
            if (cursor_y < 0) cursor_y = 0;
            if (cursor_y >= VGA_HEIGHT) cursor_y = VGA_HEIGHT - 1;
            if (cursor_x < 0) cursor_x = 0;
            if (cursor_x >= VGA_WIDTH) cursor_x = VGA_WIDTH - 1;
            set_cursor(cursor_x, cursor_y); break;
        case 'A': cursor_y -= a; if (cursor_y < 0) cursor_y = 0; set_cursor(cursor_x, cursor_y); break;
        case 'B': cursor_y += a; if (cursor_y >= VGA_HEIGHT) cursor_y = VGA_HEIGHT - 1; set_cursor(cursor_x, cursor_y); break;
        case 'C': cursor_x += a; if (cursor_x >= VGA_WIDTH) cursor_x = VGA_WIDTH - 1; set_cursor(cursor_x, cursor_y); break;
        case 'D': cursor_x -= a; if (cursor_x < 0) cursor_x = 0; set_cursor(cursor_x, cursor_y); break;
        case 'J': {   /* 2: everything; 0: from the cursor */
            elf_nested_tui();
            int from = params[0] == 2 || params[0] == 3 ? 0 : cursor_y * VGA_WIDTH + cursor_x;
            for (int k = from; k < VGA_WIDTH * VGA_HEIGHT; k++) vga_buffer[k] = (MAKE_ATTR(WHITE) << 8) | ' ';
            if (params[0] == 2 || params[0] == 3) { cursor_x = cursor_y = 0; }
            set_cursor(cursor_x, cursor_y); break;
        }
        case 'K': {
            int from = params[0] == 2 ? 0 : cursor_x, to = params[0] == 1 ? cursor_x + 1 : VGA_WIDTH;
            for (int k = from; k < to; k++) vga_buffer[cursor_y * VGA_WIDTH + k] = (MAKE_ATTR(WHITE) << 8) | ' ';
            console_present(); break;
        }
        case 'h': case 'l':
            if (private_mode && params[0] == 25) console_cursor_visible(c == 'h');
            break;
        case 'm':
            if (!count) { count = 1; params[0] = 0; }
            for (int k = 0; k < count; k++) {
                int v = params[k] < 0 ? 0 : params[k];
                if (v == 0) { bold = reverse = 0; fg = bg = -1; }
                else if (v == 1) bold = 1;
                else if (v == 22) bold = 0;
                else if (v == 7) reverse = 1;
                else if (v == 27) reverse = 0;
                else if (v >= 30 && v <= 37) fg = vga[v - 30];
                else if (v >= 90 && v <= 97) fg = vga[v - 90] + 8;
                else if (v == 39) fg = -1;
                else if (v >= 40 && v <= 47) bg = vga[v - 40];
                else if (v >= 100 && v <= 107) bg = vga[v - 100];
                else if (v == 49) bg = -1;
            }
            if (!bold && fg < 0 && bg < 0 && !reverse) ansi_attribute = -1;
            else {
                int f = fg < 0 ? theme_foreground : fg, g = bg < 0 ? theme_background : bg;
                if (bold && f < 8) f += 8;
                if (reverse) { int t = f & 7; f = g; g = t; }
                ansi_attribute = (g & 7) << 4 | (f & 15);
            }
            break;
        }
    }
    FLUSH();
#undef FLUSH
}
/* Back to the theme's colors (a program ended in the middle of a color). */
void console_ansi_reset(void) { ansi_attribute = -1; }

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
            /* A caller's buffer may be shorter than a remembered line (login name). */
            if (history_position < (int)input_history_count) { strncpy(buffer, input_history[history_position], max_length - 1); buffer[max_length - 1] = 0; }
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
                    "echo","panic","sync","mount","format","unmount","help","init","macro",
                    "bnm","get","git","brm","true","false","linux",
                    "su","sudo","chmod","useradd","whoami","theme","snake","pong","paint","fdisk"
                };
                int match = -1, matches = 0;
                for (unsigned int i = 0; i < sizeof(commands) / sizeof(commands[0]); i++)
                    if (!strncmp(commands[i], buffer, pos)) { match = (int)i; matches++; }
                if (matches == 1 && strlen(commands[match]) < max_length) { strcpy(buffer, commands[match]); pos = strlen(buffer); }
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
            /* Lines longer than a history slot (a Linux program reads up to 255) are not remembered. */
            if (pos && pos < (int)sizeof(input_history[0]) && !strstr(buffer,"passwd=") && (!input_history_count || strcmp(input_history[input_history_count - 1], buffer))) {
                if (input_history_count == 16) {
                    for (int i = 1; i < 16; i++) strcpy(input_history[i - 1], input_history[i]);
                    input_history_count--;
                }
                strcpy(input_history[input_history_count++], buffer);
            }
            break;
        }
        
        /* Ctrl+V pastes the clipboard's first line; Ctrl+C copies the
         * typed line, Ctrl+X cuts it. */
        if (ch == 22) {
            static char pasted[256];
            clipboard_get(pasted, sizeof(pasted));
            for (char *p = pasted; *p && *p != '\n' && *p != '\r'; ) {
                unsigned char c = (unsigned char)*p;
                int n = c < 0x80 ? 1 : (c & 0xe0) == 0xc0 ? 2 : (c & 0xf0) == 0xe0 ? 3 : 4;
                if (c < 32 && c != '\t') { p++; continue; }
                if (pos + n >= max_length) break;
                char one[5] = {0};
                int copied = 0;
                for (; copied < n && p[copied]; copied++) { one[copied] = p[copied]; buffer[pos++] = p[copied]; }
                buffer[pos] = 0;
                print(WHITE, c == '\t' ? " " : one);
                p += copied;   /* a sequence cut by the end of the text must not step over its NUL */
            }
            continue;
        }
        if ((ch == 3 || ch == 24) && pos) {
            clipboard_set(buffer, (unsigned int)pos);
            if (ch == 24) {
                pos = 0; buffer[0] = 0;
                for (int y = start_y; y < start_y + 3 && y < VGA_HEIGHT; y++)
                    for (int x = y == start_y ? start_x : 0; x < VGA_WIDTH; x++)
                        vga_buffer[y * VGA_WIDTH + x] = (MAKE_ATTR(WHITE) << 8) | ' ';
                set_cursor(start_x, start_y);
            }
            continue;
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

/* Копирование памяти: строковые инструкции по 4 байта за шаг (кэш ext2,
 * буфер DMA и кадры экрана копируются килобайтами). */
void *memcpy(void *dst, const void *src, int size)
{
    void *d = dst; const void *s = src;
    unsigned int words = (unsigned int)size >> 2, bytes = size & 3;
    __asm__ volatile("cld; rep movsl; mov %3, %%ecx; rep movsb"
                     : "+D"(d), "+S"(s), "+c"(words) : "r"(bytes) : "memory");
    return dst;
}

/* Перемещение памяти (поддерживает перекрытие) */
void *memmove(void *dst, const void *src, int size)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;

    if (d <= s || d >= s + size)
        return memcpy(dst, src, size);

    /* Приёмник выше источника: копируем с конца. */
    unsigned int bytes = size & 3, words = (unsigned int)size >> 2;
    d += size - 1; s += size - 1;
    __asm__ volatile("std; rep movsb; sub $3, %%esi; sub $3, %%edi; mov %3, %%ecx; rep movsl; cld"
                     : "+D"(d), "+S"(s), "+c"(bytes) : "r"(words) : "memory");
    return dst;
}

/* Заполнение памяти */
void *memset(void *dst, int value, int size)
{
    void *d = dst;
    unsigned int pattern = (unsigned char)value * 0x01010101U;
    unsigned int words = (unsigned int)size >> 2, bytes = size & 3;
    __asm__ volatile("cld; rep stosl; mov %3, %%ecx; rep stosb"
                     : "+D"(d), "+c"(words) : "a"(pattern), "r"(bytes) : "memory");
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

    /* Unsigned magnitude: -value overflows for INT_MIN. */
    unsigned int magnitude = (unsigned int)value;
    if (value < 0)
    {
        neg = 1;
        magnitude = 0U - magnitude;
    }

    while (magnitude > 0)
    {
        buffer[i++] = '0' + (magnitude % 10);
        magnitude /= 10;
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
    video_leave();   /* a program may have the screen in graphics mode */
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
