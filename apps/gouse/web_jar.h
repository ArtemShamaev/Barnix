/* Gouse Web: cookies and saved passwords (web_jar.c). Times are seconds
 * since 1970 (UTC). */
#ifndef WEB_JAR_H
#define WEB_JAR_H
int jar_init(void *memory, unsigned int bytes);
unsigned int jar_bytes(void);
unsigned int jar_epoch(int year, int month, int day, int hour, int minute, int second);

void jar_set_cookie(const char *url, const char *set_cookie_value, unsigned int now);
/* "Cookie: a=1; b=2\r\n" for url, or "": the number of cookies. */
int jar_cookie_header(const char *url, char *out, int capacity, unsigned int now);
int jar_cookie_count(void);
void jar_clear_cookies(void);
int jar_cookies_dirty(void);
void jar_load_cookies(const char *text, unsigned int now);
int jar_save_cookies(char *out, int capacity, unsigned int now);   /* cookies with an expiry date */

int jar_password(const char *url, char *user_field, char *user, char *password_field, char *password, int capacity);
int jar_password_known(const char *url, const char *user, const char *password);
void jar_save_password(const char *url, const char *user_field, const char *user, const char *password_field, const char *password);
int jar_password_count(void);
const char *jar_password_site(int index, const char **user);
void jar_clear_passwords(void);
int jar_passwords_dirty(void);
void jar_load_passwords(const char *text);
int jar_save_passwords(char *out, int capacity);
#endif
