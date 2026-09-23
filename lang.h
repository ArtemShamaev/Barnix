#ifndef LANG_H
#define LANG_H
const char *tr(const char *english);
int system_init(void);
int system_language(void);
int language_parse(const char *text, unsigned int size);
#endif
