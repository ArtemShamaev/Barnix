#ifndef TEXT_H
#define TEXT_H
/* UTF-8 stream decoder: accepts split writes; malformed input becomes '?'. */
typedef struct { unsigned int value, minimum, remaining; } Utf8Decoder;
int utf8_feed(Utf8Decoder *state, unsigned char byte, unsigned int out[2]);
unsigned int utf8_encode(unsigned int codepoint, char out[4]);
unsigned char text_glyph(unsigned int codepoint);
void console_font_init(void);
#endif
