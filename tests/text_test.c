#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "text.h"
#include "keyboard.h"
#include "lang.h"

static const char *config = "LANG=en\n";
static int missing;
int fs_size(const char *name) { assert(!strcmp(name, "/etc/sys-lang.cfg")); return missing ? -1 : (int)strlen(config); }
int fs_read(const char *name, unsigned int offset, void *data, unsigned int size)
{ (void)name; (void)offset; memcpy(data, config, size); return size; }
void println(int color, const char *text) { (void)color; (void)text; }
static void key(unsigned char c) { assert(keyboard_feed(c) == 0); }
int main(void)
{
    keyboard_reset();
    assert(keyboard_feed(0x10) == 'q');
    key(0x2a); key(0x38); key(0x38); /* Shift then Alt, repeat */
    assert(keyboard_layout() == 1);
    key(0xb8); key(0xaa);
    assert(keyboard_feed(0x10) == 0x439);
    key(0x3a); key(0x3a); key(0xba);
    assert(keyboard_feed(0x10) == 0x419);
    key(0x2a); assert(keyboard_feed(0x10) == 0x439); key(0xaa);
    key(0x3a); key(0xba);
    key(0xe0); key(0x38); key(0x36); /* Right Alt then right Shift */
    assert(keyboard_layout() == 0);
    key(0xb6); key(0xe0); key(0xb8);
    key(0x2a); key(0x36); key(0xaa);
    assert(keyboard_feed(0x10) == 'Q'); key(0xb6);
    assert(keyboard_feed(0x10) == 'q');
    key(0xe0); assert(keyboard_feed(0x48) == KEY_UP); /* arrow is not printable */
    keyboard_set_layout(1);
    assert(keyboard_feed(0x29) == 0x451);
    key(0x2a); assert(keyboard_feed(0x29) == 0x401);
    assert(keyboard_feed(0x04) == 0x2116); key(0xaa);
    for (unsigned int c = 0x400; c <= 0x45f; c++) {
        char bytes[4]; unsigned int out[2]; Utf8Decoder state = {0};
        unsigned int n = utf8_encode(c, bytes);
        assert(n == 2);
        assert(utf8_feed(&state, bytes[0], out) == 0);
        assert(utf8_feed(&state, bytes[1], out) == 1 && out[0] == c);
    }
    Utf8Decoder state = {0}; unsigned int out[2];
    assert(utf8_feed(&state, 0xd0, out) == 0);
    assert(utf8_feed(&state, 'x', out) == 2 && out[0] == '?' && out[1] == 'x');
    assert(utf8_feed(&state, 0x80, out) == 1 && out[0] == '?');
    assert(utf8_feed(&state, 0xe0, out) == 0);
    assert(utf8_feed(&state, 0x80, out) == 0);
    assert(utf8_feed(&state, 0x80, out) == 1 && out[0] == '?');
    assert(text_glyph(0x410) == 0x80 && text_glyph(0x44f) == 0xef);
    assert(text_glyph(0x401) == 0xf0 && text_glyph(0x451) == 0xf1);
    assert(text_glyph(0x1f600) == '?');
    const char *valid = "\xef\xbb\xbf# comment\r\n LANG = ru # x\r\n";
    assert(language_parse(valid, strlen(valid)) == 1);
    assert(language_parse("en", 2) == 0);
    assert(language_parse("LANG=ru\nLANG=en", 15) == -1);
    assert(language_parse("LANG=fr", 7) == -1);
    assert(language_parse("LANG=ru\0junk", 12) == -1);
    assert(language_parse("", 0) == -1);
    config = "LANG=ru"; assert(system_init() == 0 && system_language() == 1);
    assert(!strcmp(tr("configuration applied"), "Настройки применены"));
    config = "broken"; assert(system_init() == -1 && system_language() == 1);
    missing = 1; assert(system_init() == -1 && system_language() == 1);
    missing = 0; config = "en"; assert(system_init() == 0 && system_language() == 0);
    puts("Keyboard, UTF-8 and configuration tests passed");
    return 0;
}
