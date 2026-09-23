#include "command.h"
#include "keyboard.h"
#include "text.h"

void *memcpy(void *dst, const void *src, int size);
void *memmove(void *dst, const void *src, int size);
int memcmp(const void *a, const void *b, int size);

#define EDITOR_BYTES 32768
#define EDITOR_LINES 21
#define CONTENT_ROW 2
#define STATUS_ROW 23
#define HELP_ROW 24

static char document[EDITOR_BYTES];
static char scratch[128];
static unsigned int length, position, top;
static const char *filename;
static const char *status;

static int continuation(unsigned char c) { return (c & 0xc0) == 0x80; }
static unsigned int next_char(unsigned int p)
{
    if (p >= length) return length;
    p++;
    while (p < length && continuation((unsigned char)document[p])) p++;
    return p;
}
static unsigned int previous_char(unsigned int p)
{
    if (!p) return 0;
    do p--; while (p && continuation((unsigned char)document[p]));
    return p;
}
static unsigned int line_start(unsigned int p)
{
    while (p && document[p - 1] != '\n') p--;
    return p;
}
static unsigned int line_end(unsigned int p)
{
    while (p < length && document[p] != '\n') p++;
    return p;
}
static unsigned int line_number(unsigned int p)
{
    unsigned int n = 0;
    for (unsigned int i = 0; i < p; i++) if (document[i] == '\n') n++;
    return n;
}
static unsigned int start_of_line_number(unsigned int number)
{
    unsigned int p = 0;
    while (number && p < length) { if (document[p++] == '\n') number--; }
    return p;
}
static unsigned int column(unsigned int p)
{
    unsigned int begin = line_start(p), n = 0;
    for (; begin < p; n++) begin = next_char(begin);
    return n;
}
static unsigned int position_at_column(unsigned int begin, unsigned int wanted)
{
    unsigned int p = begin;
    while (wanted-- && p < length && document[p] != '\n') p = next_char(p);
    return p;
}
static void insert_bytes(unsigned int at, const char *data, unsigned int count)
{
    if (count > EDITOR_BYTES - length) return;
    memmove(document + at + count, document + at, length - at);
    memcpy(document + at, data, count); length += count; position = at + count;
}
static void remove_bytes(unsigned int at, unsigned int count)
{
    if (count > length - at) count = length - at;
    memmove(document + at, document + at + count, length - at - count);
    length -= count; position = at;
}
static void clear_row(int row)
{
    barnix->goto_xy(0, row);
    for (int i = 0; i < 80; i++) barnix->print(15, " ");
}
static void draw_line(unsigned int p, int row, int current)
{
    char line[128]; unsigned int n = 0;
    unsigned int end = line_end(p);
    while (p < end && n < 78) {
        unsigned int q = next_char(p), bytes = q - p;
        if (n + bytes > 78) break;
        memcpy(line + n, document + p, bytes); n += bytes; p = q;
    }
    line[n] = 0;
    clear_row(row);
    barnix->goto_xy(0, row);
    barnix->print(current ? 14 : 15, line);
}
static void draw(void)
{
    barnix->clear();
    barnix->goto_xy(0, 0); barnix->print(11, "Barnino System(s) Macro text editor");
    barnix->goto_xy(0, 1); barnix->print(15, "File: "); barnix->print(15, filename);
    unsigned int line = 0, p = top;
    while (line < EDITOR_LINES) {
        draw_line(p, CONTENT_ROW + line, line_number(position) == line_number(p));
        if (p >= length) { line++; while (line < EDITOR_LINES) clear_row(CONTENT_ROW + line++); break; }
        p = line_end(p); if (p < length) p++;
        line++;
    }
    clear_row(STATUS_ROW); barnix->goto_xy(0, STATUS_ROW);
    barnix->print(status ? 12 : 7, status ? status : "Ready");
    clear_row(HELP_ROW); barnix->goto_xy(0, HELP_ROW);
    barnix->print(15, "^S Save   ^Q Quit   ^F Find   Arrows Move   Backspace Delete");
    unsigned int row = line_number(position) - line_number(top);
    unsigned int x = column(position);
    if (row >= EDITOR_LINES) row = EDITOR_LINES - 1;
    if (x > 78) x = 78;
    barnix->goto_xy((int)x, CONTENT_ROW + (int)row);
}
static int find_text(void)
{
    unsigned int n = 0; status = "Find: "; draw();
    for (;;) {
        unsigned int key = barnix->getch();
        if (key == '\n') break;
        if (key == KEY_ESCAPE) { status = "Find cancelled"; draw(); return 0; }
        if (key == 0x08) { if (n) { do n--; while (n && continuation((unsigned char)scratch[n])); } }
        else if (key >= 32 && key != 127) {
            char encoded[4]; unsigned int bytes = utf8_encode(key, encoded);
            if (n + bytes < sizeof(scratch)) { memcpy(scratch + n, encoded, bytes); n += bytes; }
        }
        scratch[n] = 0; status = "Find: "; draw();
        barnix->goto_xy(6 + (int)n, STATUS_ROW);
    }
    scratch[n] = 0;
    if (!n) { status = "Find cancelled"; draw(); return 0; }
    for (unsigned int i = position; i + n <= length; i++)
        if (!memcmp(document + i, scratch, n)) { position = i; status = "Match found"; draw(); return 1; }
    status = "Text not found"; draw(); return 0;
}
int main(int argc, const char *const *argv)
{
    if (argc != 2) return fail("usage: macro <file>");
    filename = argv[1];
    int size = barnix->size(filename);
    if (size > (int)sizeof(document)) return fail("file is too large for macro");
    if (size > 0 && barnix->read(filename, 0, document, size) != size) return fail("cannot read file");
    length = size > 0 ? (unsigned int)size : 0; position = top = 0; status = "Ready";
    for (;;) {
        unsigned int key;
        draw(); key = barnix->getch(); status = NULL;
        if (key == 19) { if (barnix->write(filename, document, (int)length)) status = "Save failed"; else status = "Saved"; continue; }
        if (key == 17) return 0;
        if (key == 6) { find_text(); continue; }
        if (key == KEY_LEFT) position = previous_char(position);
        else if (key == KEY_RIGHT) position = next_char(position);
        else if (key == KEY_HOME) position = line_start(position);
        else if (key == KEY_END) position = line_end(position);
        else if (key == KEY_UP || key == KEY_DOWN) {
            unsigned int target = line_number(position);
            unsigned int col = column(position);
            if (key == KEY_UP && target) target--; else if (key == KEY_DOWN) target++;
            unsigned int begin = start_of_line_number(target);
            position = position_at_column(begin, col);
        } else if (key == 0x08) position = previous_char(position), remove_bytes(position, next_char(position) - position);
        else if (key == KEY_DELETE) remove_bytes(position, next_char(position) - position);
        else if (key == '\n') insert_bytes(position, "\n", 1);
        else if (key >= 32 && key != 127) {
            char encoded[4]; unsigned int n = utf8_encode(key, encoded); insert_bytes(position, encoded, n);
        }
        unsigned int current = line_number(position);
        if (current < line_number(top)) top = line_start(position);
        else if (current >= line_number(top) + EDITOR_LINES) top = start_of_line_number(current - EDITOR_LINES + 1);
    }
}
