#include "stdio.h"
#include <stdarg.h>

extern const BarnixAPI *barnix;
extern int barnix_program_argc;
extern const char *const *barnix_program_argv;
static FILE streams[4];

int putchar(int character)
{
    char text[2] = {(char)character, 0};
    barnix->print(15, text);
    return character;
}
int puts(const char *text) { barnix->puts(text); return 0; }
int getchar(void) { return (int)barnix->getch(); }
int argument_count(void) { return barnix_program_argc; }
const char *argument_value(int index)
{
    if (index < 0 || index >= barnix_program_argc) return 0;
    return barnix_program_argv[index];
}
int argument_int(int index, int *value)
{
    const char *text = argument_value(index); int sign = 1, result = 0, digits = 0;
    if (!text || !value) return -1;
    if (*text == '-') { sign = -1; text++; }
    while (*text >= '0' && *text <= '9') {
        result = result * 10 + *text++ - '0'; digits++;
    }
    if (!digits || *text) return -1;
    *value = sign * result; return 0;
}
static int decimal(int value, char *out)
{
    unsigned int n = value < 0 ? (unsigned int)(-value) : (unsigned int)value;
    int p = 0;
    if (value < 0) out[p++] = '-';
    char reverse[12]; int count = 0;
    do { reverse[count++] = (char)('0' + n % 10); n /= 10; } while (n);
    while (count) out[p++] = reverse[--count];
    out[p] = 0; return p;
}
int printf(const char *format, ...)
{
    va_list args; int written = 0; va_start(args, format);
    while (*format) {
        if (*format != '%') { putchar((unsigned char)*format++); written++; continue; }
        format++;
        char number[16]; const char *text;
        if (*format == 'd') { written += decimal(va_arg(args, int), number); barnix->print(15, number); }
        else if (*format == 'c') { putchar(va_arg(args, int)); written++; }
        else if (*format == 's') { text = va_arg(args, const char *); barnix->print(15, text); while (*text++) written++; }
        else if (*format == '%') { putchar('%'); written++; }
        else { putchar('%'); putchar(*format); written += 2; }
        if (*format) format++;
    }
    va_end(args); return written;
}
static int read_line(char *line, int limit)
{
    int length = 0;
    while (length + 1 < limit) {
        unsigned int key = barnix->getch();
        if (key == '\n') break;
        if (key == 0x08) { if (length) length--; continue; }
        if (key >= 32 && key < 127) line[length++] = (char)key;
    }
    line[length] = 0; return length;
}
static const char *skip_space(const char *p) { while (*p == ' ' || *p == '\t' || *p == '\n') p++; return p; }
int scanf(const char *format, ...)
{
    char line[256]; const char *input = line; int assigned = 0;
    read_line(line, sizeof(line));
    va_list args; va_start(args, format);
    while (*format) {
        if (*format == ' ') { format++; input = skip_space(input); continue; }
        if (*format != '%') { if (*input++ != *format++) break; continue; }
        format++;
        if (*format == 'd') {
            int sign = 1, value = 0, digits = 0; input = skip_space(input);
            if (*input == '-') { sign = -1; input++; }
            while (*input >= '0' && *input <= '9') { value = value * 10 + *input++ - '0'; digits++; }
            if (!digits) break;
            *va_arg(args, int *) = sign * value;
            assigned++;
        } else if (*format == 'c') {
            input = skip_space(input); if (!*input) break;
            *va_arg(args, char *) = *input++; assigned++;
        } else if (*format == 's') {
            char *out = va_arg(args, char *); input = skip_space(input);
            if (!*input) break;
            while (*input && *input != ' ' && *input != '\t') *out++ = *input++;
            *out = 0; assigned++;
        } else break;
        format++;
    }
    va_end(args); return assigned;
}
FILE *fopen(const char *name, const char *mode)
{
    for (unsigned int i = 0; i < sizeof(streams) / sizeof(streams[0]); i++) {
        if (streams[i].name) continue;
        streams[i].name = name; streams[i].position = 0;
        streams[i].append = mode && mode[0] == 'a';
        if (mode && mode[0] == 'w' && barnix->write(name, "", 0)) {
            streams[i].name = 0;
            continue;
        }
        if (streams[i].append) {
            int size = barnix->size(name);
            if (size >= 0) streams[i].position = (unsigned int)size;
        }
        return &streams[i];
    }
    return 0;
}
int fclose(FILE *stream)
{
    if (!stream || !stream->name) return -1;
    stream->name = 0; return 0;
}
unsigned int barnix_fread(void *data, unsigned int size, unsigned int count, FILE *stream)
{
    if (!stream || !stream->name || !size || !count) return 0;
    unsigned int bytes = size * count;
    int got = barnix->read(stream->name, stream->position, data, bytes);
    if (got <= 0) return 0;
    stream->position += (unsigned int)got;
    return (unsigned int)got / size;
}
unsigned int barnix_fwrite(const void *data, unsigned int size, unsigned int count, FILE *stream)
{
    if (!stream || !stream->name || !size || !count) return 0;
    unsigned int bytes = size * count;
    int result;
    if (stream->append || stream->position) result = barnix->append(stream->name, data, (int)bytes);
    else result = barnix->write(stream->name, data, (int)bytes);
    if (result) return 0;
    stream->position += bytes;
    return count;
}
