#include "lang.h"
#include "barnix.h"
#include "fs.h"

static int language;
int system_language(void) { return language; }
static int space(char c) { return c == ' ' || c == '\t' || c == '\r'; }
/* One setting: ru/en or LANG=ru/en. Blank lines and # comments are allowed.
 * Parse everything before committing, so a bad reload preserves old state. */
int language_parse(const char *text, unsigned int size)
{
    int found = -1;
    unsigned int p = 0;
    if (size >= 3 && (unsigned char)text[0] == 0xef &&
        (unsigned char)text[1] == 0xbb && (unsigned char)text[2] == 0xbf) p = 3;
    while (p < size) {
        unsigned int start = p;
        while (p < size && text[p] != '\n') p++;
        unsigned int end = p;
        if (p < size) p++;
        for (unsigned int i = start; i < end; i++) if (text[i] == '#') { end = i; break; }
        while (start < end && space(text[start])) start++;
        while (end > start && space(text[end-1])) end--;
        if (start == end) continue;
        if (found != -1) return -1;
        if (end - start >= 4 && !strncmp(text + start, "LANG", 4)) {
            start += 4;
            while (start < end && space(text[start])) start++;
            if (start == end || text[start++] != '=') return -1;
            while (start < end && space(text[start])) start++;
        }
        if (end - start != 2) return -1;
        if (!strncmp(text + start, "ru", 2)) found = 1;
        else if (!strncmp(text + start, "en", 2)) found = 0;
        else return -1;
    }
    return found;
}
int system_init(void)
{
    char config[512];
    int size = fs_size("/etc/sys-lang.cfg");
    if (size < 0 || size > (int)sizeof(config) ||
        fs_read("/etc/sys-lang.cfg", 0, config, size) != size) {
        println(RED, tr("cannot read /etc/sys-lang.cfg; language unchanged")); return -1;
    }
    int next = language_parse(config, size);
    if (next < 0) {
        println(RED, tr("invalid /etc/sys-lang.cfg; expected LANG=en or LANG=ru")); return -1;
    }
    language = next;
    return 0;
}

typedef struct { const char *en, *ru; } Translation;
static const Translation messages[] = {
#include "translations.inc"
};
const char *tr(const char *english)
{
    if (!language || !english) return english;
    for (unsigned int i = 0; i < sizeof(messages)/sizeof(messages[0]); i++)
        if (!strcmp(english, messages[i].en)) return messages[i].ru;
    return english;
}
