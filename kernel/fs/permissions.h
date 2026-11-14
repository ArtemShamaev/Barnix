#ifndef BARNIX_PERMISSIONS_H
#define BARNIX_PERMISSIONS_H
#define MAX_USERNAME 32
#define MAX_PASSWORD 64
const char *get_current_user(void);
int permissions_root(void);
unsigned int permissions_uid(void);
int permissions_move_prepare(const char *source,const char *dest);
int permissions_move_commit(void);
void permissions_move_finish(void);
int check_permission(const char *path, char mode);
int authenticate_user(const char *user, const char *password);
int switch_user(const char *user, const char *password);
int account_create(const char *user, const char *password);
int set_file_permissions(const char *path, const char *spec, const char *password);
void permissions_restore(const char *user);
#endif
