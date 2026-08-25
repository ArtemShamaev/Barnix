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
#endif
