#ifndef FS_H
#define FS_H
#define FS_MAX_FILE_SIZE ((12 + 256) * 1024)
int fs_init(void);
int fs_mount(const char *device, const char *mountpoint);
int fs_unmount(void);
void fs_mount_info(void);
int fs_exec_check(const char *name);
int fs_size(const char *name);
int fs_read(const char *name, unsigned int offset, void *buffer, unsigned int size);
void fs_ls(void);
void fs_cat(const char *name);
int fs_touch(const char *name);
int fs_write(const char *name, const char *data, int size);
int fs_rm(const char *name);
int fs_mkdir(const char *name);
int fs_cd(const char *name);
void fs_pwd(void);
int fs_getcwd(char *out, unsigned int capacity);
void fs_df(void);
int fs_stat(const char *name);
int fs_append(const char *name, const char *data, int size);
int fs_cp(const char *source, const char *destination);
int fs_mv(const char *source, const char *destination);
int fs_rmdir(const char *name);
int fs_sync(void);
int fs_is_dir(const char *name);
int fs_getdents(const char *name, void *buffer, unsigned int capacity, unsigned int *position);
#endif
