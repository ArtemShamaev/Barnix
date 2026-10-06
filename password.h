#ifndef BARNIX_PASSWORD_H
#define BARNIX_PASSWORD_H
/* Salted iterated SHA-256. Legacy plaintext records are accepted for migration. */
void password_encode(const char *password, const char *salt, char out[96]);
int password_matches(const char *password, const char *encoded);
#endif
