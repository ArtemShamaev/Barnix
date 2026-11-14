#ifndef BARNIX_SETTINGS_H
#define BARNIX_SETTINGS_H
int theme_set(const char *name);
void settings_load(void);
void user_config_path(const char *file, char *out);
void console_theme(int background, int foreground);
int console_attribute(int color);
#endif
