#ifndef FS_H
#define FS_H
#define FS_MAX_BLOCKS (5U * 1024U * 1024U)
int fs_fdisk(int argc, const char *const *argv);
int fs_format_selected(void);
int fs_import_template(const void *image, unsigned int bytes);
#define FS_MAX_FILE_SIZE ((12 + 256) * 1024)
int fs_init(void);
int fs_format(const char *device);
int fs_unmount_path(const char *path);
int fs_mount(const char *device, const char *mountpoint);
int fs_unmount(void);
void fs_mount_info(void);
int fs_file_size(const char *path);
int fs_pwrite(const char *path, unsigned int position, const void *bytes, unsigned int count, int append);
int fs_load_executable(const char *path, void *buffer, unsigned int capacity);
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
void fs_disk_info(void);
void fs_lsblk(void);
int fs_stat(const char *name);
int fs_append(const char *name, const char *data, int size);
int fs_cp(const char *source, const char *destination);
int fs_mv(const char *source, const char *destination);
int fs_rmdir(const char *name);
int fs_sync(void);
int fs_is_dir(const char *name);
int fs_getdents(const char *name, void *buffer, unsigned int capacity, unsigned int *position);
#endif
