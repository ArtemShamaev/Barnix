#include "stdio.h"
#include <stdarg.h>

extern const BarnixAPI *barnix;
extern int barnix_program_argc;
extern const char *const *barnix_program_argv;
static FILE streams[4];
static FILE console_input={"",0,0}, console_output={"",0,0}, console_error={"",0,0};
FILE *stdin=&console_input, *stdout=&console_output, *stderr=&console_error;

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
static int format_output(FILE *stream, char *buffer, int capacity, const char *format, va_list args)
{
    int written = 0;
    while (*format) {
        if (*format != '%') {
            if (buffer) { if (written + 1 < capacity) buffer[written]=(char)*format; }
            else if (stream) fputc((unsigned char)*format,stream);
            written++; format++; continue;
        }
        format++;
        char number[16]; const char *text;
        if (*format == 'd') { int n=decimal(va_arg(args, int),number); for(int i=0;i<n;i++){if(buffer){if(written+i+1<capacity)buffer[written+i]=number[i];}else if(stream)fputc(number[i],stream);} written+=n; }
        else if (*format == 'u') { unsigned int value=va_arg(args,unsigned int); int n=0; do{number[n++]=(char)('0'+value%10);value/=10;}while(value); for(int i=n-1;i>=0;i--){if(buffer){if(written+n-1-i+1<capacity)buffer[written+n-1-i]=number[i];}else if(stream)fputc(number[i],stream);} written+=n; }
        else if (*format == 'c') { char c=(char)va_arg(args,int);if(buffer){if(written+1<capacity)buffer[written]=c;}else if(stream)fputc(c,stream);written++; }
        else if (*format == 's') { text = va_arg(args, const char *);if(!text)text="(null)";while(*text){if(buffer){if(written+1<capacity)buffer[written]=*text;}else if(stream)fputc(*text,stream);written++;text++;} }
        else if (*format == '%') {if(buffer){if(written+1<capacity)buffer[written]='%';}else if(stream)fputc('%',stream);written++; }
        else {if(buffer){if(written+1<capacity)buffer[written]='%';if(written+2<capacity)buffer[written+1]=*format;}else if(stream){fputc('%',stream);fputc(*format,stream);}written+=2;}
        if (*format) format++;
    }
    if(buffer&&capacity>0)buffer[written<capacity?written:capacity-1]=0;
    return written;
}
int vprintf(const char *format, va_list args) { return format_output(stdout,0,0,format,args); }
int printf(const char *format, ...)
{
    va_list args;va_start(args,format);int result=vprintf(format,args);va_end(args);return result;
}
int fprintf(FILE *stream,const char *format,...)
{
    va_list args;va_start(args,format);int result=format_output(stream,0,0,format,args);va_end(args);return result;
}
int vsnprintf(char *buffer,size_t capacity,const char *format,va_list args) { return format_output(0,buffer,(int)capacity,format,args); }
int snprintf(char *buffer,size_t capacity,const char *format,...)
{
    va_list args;va_start(args,format);int result=vsnprintf(buffer,capacity,format,args);va_end(args);return result;
}
int fputc(int character,FILE *stream) {
    if(stream==stdout||stream==stderr||!stream||!stream->name){if(stream==stdout||stream==stderr)return putchar(character);return EOF;}
    unsigned char value=(unsigned char)character;return barnix_fwrite(&value,1,1,stream)==1?character:EOF;
}
int fputs(const char *text,FILE *stream) {if(!text)return EOF;int n=0;while(text[n]){if(fputc((unsigned char)text[n],stream)==EOF)return EOF;n++;}return n;}
int fgetc(FILE *stream) {unsigned char value;return barnix_fread(&value,1,1,stream)==1?value:EOF;}
int fflush(FILE *stream) {(void)stream;return 0;}
int feof(FILE *stream) {return !stream||!stream->name;}
int ferror(FILE *stream) {(void)stream;return 0;}
int fseek(FILE *stream,long offset,int origin) {
    if(!stream||!stream->name||offset<0)return -1;
    long base=origin==SEEK_CUR?(long)stream->position:origin==SEEK_END?barnix->size(stream->name):0;
    if(base+offset<0)return -1;
    stream->position=(unsigned int)(base+offset);return 0;
}
long ftell(FILE *stream) {return stream&&stream->name?(long)stream->position:-1;}
int remove(const char *name) {return barnix->rm(name);}
int rename(const char *old_name,const char *new_name) {return barnix->mv(old_name,new_name);}
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
    if (!name || !mode || !mode[0]) return 0;
    int existing=barnix->size(name)>=0;
    if (mode[0]=='r' && !existing) return 0;
    if (mode[0]=='a' && !existing && barnix->write(name,"",0)) return 0;
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
