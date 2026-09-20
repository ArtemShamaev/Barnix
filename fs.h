#ifndef FS_H
#define FS_H
int fs_init(void);
void fs_ls(void);
void fs_cat(const char *name);
int fs_touch(const char *name);
int fs_write(const char *name, const char *data, int size);
int fs_rm(const char *name);
int fs_mkdir(const char *name);
int fs_cd(const char *name);
void fs_pwd(void);
void fs_df(void);
int fs_stat(const char *name);
int fs_append(const char *name, const char *data, int size);
int fs_cp(const char *source, const char *destination);
int fs_mv(const char *source, const char *destination);
int fs_rmdir(const char *name);
int fs_sync(void);
#endif
