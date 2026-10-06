/* Namespace routing over the ext2 engine. Only one image is cached: switching
 * volumes reloads it, while every mutation is already committed by ext2. */
#define MOUNT_COUNT 8
#define PATH_CAP 256
static struct { int used; DiskSelection disk; char path[PATH_CAP]; } mounts[MOUNT_COUNT];
static char working[PATH_CAP] = "/";
static int namespace_active, active_volume = -1;
static int normalize(const char *name, char *out)
{
    if (!name || !*name) return -1;
    char input[PATH_CAP * 2];
    int n = strlen(name);
    if (n >= PATH_CAP) return -1;
    input[0] = 0;
    if (*name != '/') { strcpy(input, working); if (strcmp(input, "/")) strcpy(input + strlen(input), "/"); }
    strcpy(input + strlen(input), name);
    int len = 1; out[0] = '/'; out[1] = 0;
    const char *p = input;
    while (*p) {
        while (*p == '/') p++;
        const char *start = p; while (*p && *p != '/') p++;
        int size = p - start;
        if (!size || (size == 1 && *start == '.')) continue;
        if (size == 2 && start[0] == '.' && start[1] == '.') {
            while (len > 1 && out[len - 1] != '/') len--;
            if (len > 1) len--;
        } else {
            if (size > MAX_NAME || len + size + 1 >= PATH_CAP) return -1;
            if (len > 1) out[len++] = '/';
            memcpy(out + len, start, size); len += size;
        }
        out[len] = 0;
    }
    return 0;
}
static int beneath(const char *path, const char *prefix)
{
    int n = strlen(prefix);
    return !strcmp(prefix, "/") || (!strncmp(path, prefix, n) && (!path[n] || path[n] == '/'));
}
static void namespace_start(void)
{
    if (namespace_active) return;
    memset(mounts, 0, sizeof(mounts));
    mounts[0].used = engine_ready(); mounts[0].disk = disk_selection(); strcpy(mounts[0].path, "/");
    if (ext_getcwd(working, sizeof(working)) < 0) strcpy(working, "/");
    active_volume = mounts[0].used ? 0 : -1; namespace_active = 1;
}
static int volume_for(const char *path)
{
    int best = -1;
    for (int i = 0; i < MOUNT_COUNT; i++)
        if (mounts[i].used && beneath(path, mounts[i].path) &&
            (best < 0 || strlen(mounts[i].path) > strlen(mounts[best].path))) best = i;
    return best;
}
static int activate(int index)
{
    if (index < 0) return -1;
    if (active_volume != index) {
        disk_restore(mounts[index].disk); active_volume = -1;
        if (ext_init()) return -1;
        active_volume = index;
    }
    return engine_ready() ? 0 : -1;
}
static int route(const char *name, char *local, int parent)
{
    char path[PATH_CAP];
    if (normalize(name, path)) return -1;
    int v = volume_for(path);
    if (activate(v)) return -1;
    const char *p = path + (strcmp(mounts[v].path, "/") ? strlen(mounts[v].path) : 0);
    if (!*p) p = "/";
    strcpy(local, p);
    cwd = ROOT_INO;
    if (parent) {
        char *slash = local;
        for (char *s = local; *s; s++) if (*s == '/') slash = s;
        char base[MAX_NAME + 1]; strcpy(base, slash + 1);
        if (!*base) return -1;
        if (slash == local) slash[1] = 0; else *slash = 0;
        if (ext_cd(local)) return -1;
        strcpy(local, base);
    }
    return v;
}
static int protects_mount(const char *name)
{
    char path[PATH_CAP]; if (normalize(name, path)) return 1;
    for (int i = 0; i < MOUNT_COUNT; i++)
        if (mounts[i].used && beneath(mounts[i].path, path)) return 1;
    return beneath(working, path);
}
int fs_init(void) { namespace_active = 0; active_volume = -1; strcpy(working, "/"); return ext_init(); }
#define PATH_WRAPPER(fn, parent, mode) \
int fs_##fn(const char *name) { \
    if (!check_permission(name, mode)) return -13; \
    if (!namespace_active) return ext_##fn(name); \
    char local[PATH_CAP]; if (route(name, local, parent) < 0) return -1; \
    return ext_##fn(local); }
PATH_WRAPPER(size, 0, 'r')
PATH_WRAPPER(exec_check, 0, 'x')
PATH_WRAPPER(stat, 0, 'r')
PATH_WRAPPER(touch, 1, 'w')
PATH_WRAPPER(mkdir, 1, 'w')
int fs_is_dir(const char *name) {
    if (!check_permission(name, 'x')) return 0;
    if (!namespace_active) return ext_is_dir(name);
    char local[PATH_CAP]; return route(name, local, 0) < 0 ? 0 : ext_is_dir(local);
}
int fs_rm(const char *name) {
    if (!check_permission(name, 'w')) return -13;
    if (!namespace_active) return ext_rm(name);
    char local[PATH_CAP]; if (protects_mount(name) || route(name, local, 1) < 0) return -1;
    return ext_rm(local);
}
int fs_rmdir(const char *name) {
    if (!check_permission(name, 'w')) return -13;
    if (!namespace_active) return ext_rmdir(name);
    char local[PATH_CAP]; if (protects_mount(name) || route(name, local, 1) < 0) return -1;
    return ext_rmdir(local);
}
int fs_read(const char *name, unsigned int offset, void *buffer, unsigned int size) {
    if (!check_permission(name, 'r')) return -13;
    if (!namespace_active) return ext_read(name, offset, buffer, size);
    char local[PATH_CAP]; if (route(name, local, 0) < 0) return -1;
    return ext_read(local, offset, buffer, size);
}
int fs_getdents(const char *name, void *buffer, unsigned int capacity, unsigned int *position) {
    if (!check_permission(name, 'r')) return -13;
    if (!namespace_active) return ext_getdents(name, buffer, capacity, position);
    char local[PATH_CAP]; if (route(name, local, 0) < 0) return -1;
    return ext_getdents(local, buffer, capacity, position);
}
#define WRITE_WRAPPER(fn) \
int fs_##fn(const char *name, const char *data, int size) { \
    if (!check_permission(name, 'w')) return -13; \
    if (!namespace_active) return ext_##fn(name, data, size); \
    char local[PATH_CAP]; if (route(name, local, 1) < 0) return -1; \
    return ext_##fn(local, data, size); }
WRITE_WRAPPER(write)
WRITE_WRAPPER(append)
int fs_cd(const char *name) {
    if (!check_permission(name, 'x')) return -13;
    if (!namespace_active) return ext_cd(name);
    char path[PATH_CAP]; if (normalize(name, path) || !fs_is_dir(path)) return -1;
    strcpy(working, path); return 0;
}
int fs_getcwd(char *out, unsigned int capacity) {
    if (!namespace_active) return ext_getcwd(out, capacity);
    unsigned int n = strlen(working) + 1;
    if (!out || n > capacity) return -34;
    strcpy(out, working); return n;
}
void fs_pwd(void) { if (!namespace_active) ext_pwd(); else println(WHITE, working); }
void fs_ls(void) {
    if (!check_permission(".", 'r')) { println(RED, "Permission denied"); return; }
    if (!namespace_active) { ext_ls(); return; }
    char local[PATH_CAP]; if (route(working, local, 0) >= 0 && !ext_cd(local)) ext_ls();
}
void fs_df(void) {
    if (!namespace_active) { ext_df(); return; }
    char local[PATH_CAP]; if (route(working, local, 0) >= 0) ext_df();
}
void fs_cat(const char *name) {
    if (!check_permission(name, 'r')) { println(RED, "Permission denied"); return; }
    if (!namespace_active) { ext_cat(name); return; }
    char local[PATH_CAP]; if (route(name, local, 0) >= 0) ext_cat(local);
}
int fs_cp(const char *source, const char *dest) {
    if (!check_permission(source, 'r') || !check_permission(dest, 'w')) return -13;
    if (!namespace_active) return ext_cp(source, dest);
    static char buffer[MAX_FILE];
    int size = fs_size(source);
    if (size < 0 || fs_size(dest) >= 0 || fs_is_dir(dest) || fs_read(source, 0, buffer, size) != size) return -1;
    return fs_write(dest, buffer, size);
}
int fs_mv(const char *source, const char *dest) {
    if (!check_permission(source, 'w') || !check_permission(dest, 'w')) return -13;
    if (permissions_move_prepare(source,dest)) return -13;
    if (!namespace_active) return ext_mv(source, dest);
    char a[PATH_CAP], b[PATH_CAP];
    if (protects_mount(source) || normalize(source, a) || normalize(dest, b)) return -1;
    int va = volume_for(a), vb = volume_for(b);
    if (va != vb) return -1;
    char src[PATH_CAP], dst[PATH_CAP];
    if (route(source, src, 1) < 0) return -1;
    unsigned int parent = cwd;
    if (route(dest, dst, 1) < 0 || cwd != parent) return -1;
    if(permissions_move_commit())return -1;
    /* Publishing ACLs changes the cached directory; select the source again. */
    if(route(source,src,1)<0)return -1;
    int result=ext_mv(src,dst);
    if(!result)permissions_move_finish();
    return result;
}
int fs_sync(void) {
    if (!namespace_active) return ext_sync();
    int result = 0;
    for (int i = 0; i < MOUNT_COUNT; i++) if (mounts[i].used && (activate(i) || ext_sync())) result = -1;
    return result;
}
int fs_mount(const char *device, const char *mountpoint) {
    if (!permissions_root()) return -13;
    namespace_start();
    char path[PATH_CAP]; if (normalize(mountpoint, path)) return -1;
    int root = !strcmp(path, "/"), slot = -1;
    for (int i = 0; i < MOUNT_COUNT; i++) {
        if (mounts[i].used && !strcmp(mounts[i].path, path)) { if (!root) return -1; slot = i; break; }
        if (!mounts[i].used && slot < 0) slot = i;
    }
    if (slot < 0 || (!root && !fs_is_dir(path))) return -1;
    if (root) for (int i = 0; i < MOUNT_COUNT; i++) if (mounts[i].used && strcmp(mounts[i].path, "/")) return -1;
    if (disk_select(device)) return -1;
    DiskSelection next = disk_selection(); active_volume = -1;
    for (int i = 0; i < MOUNT_COUNT; i++) if (mounts[i].used && i != slot && mounts[i].disk.id == next.id && mounts[i].disk.offset == next.offset) return -1;
    if (ext_init()) return -1;
    mounts[slot].used = 1; mounts[slot].disk = next; strcpy(mounts[slot].path, path);
    active_volume = slot;
    if (root) strcpy(working, "/");
    return 0;
}
int fs_unmount_path(const char *name) {
    if (!permissions_root()) return -13;
    namespace_start();
    char path[PATH_CAP]; if (normalize(name ? name : working, path)) return -1;
    int v = volume_for(path);
    if (v < 0 || (name && strcmp(path, mounts[v].path))) return -1;
    for (int i = 0; i < MOUNT_COUNT; i++)
        if (i != v && mounts[i].used && beneath(mounts[i].path, mounts[v].path)) return -1;
    disk_restore(mounts[v].disk); active_volume = -1;
    if (disk_present() && (activate(v) || ext_sync())) return -1;
    mounts[v].used = 0; active_volume = -1; mounted = 0; disk_deselect();
    if (beneath(working, mounts[v].path)) strcpy(working, "/");
    return 0;
}
int fs_unmount(void) { return fs_unmount_path(NULL); }
void fs_mount_info(void) {
    namespace_start(); int any = 0;
    for (int i = 0; i < MOUNT_COUNT; i++) if (mounts[i].used) {
        DiskSelection previous = disk_selection(); disk_restore(mounts[i].disk);
        print(WHITE, tr("mounted ")); print(WHITE, disk_name()); print(WHITE, tr(" on "));
        print(WHITE, mounts[i].path); println(WHITE, " (ext2)"); disk_restore(previous); any = 1;
    }
    if (!any) println(WHITE, tr("no filesystem mounted"));
}
int fs_format(const char *device)
{
    if (!permissions_root()) return -13;
    namespace_start();
    if (disk_select_raw(device)) return -1;
    DiskSelection target = disk_selection(); active_volume = -1;
    for (int i = 0; i < MOUNT_COUNT; i++)
        if (mounts[i].used && mounts[i].disk.id == target.id &&
            target.offset < mounts[i].disk.offset + mounts[i].disk.sectors &&
            mounts[i].disk.offset < target.offset + target.sectors) return -1;
    return fs_format_selected();
}

void fs_disk_info(void) {
    if (namespace_active) {
        char local[PATH_CAP];
        if (route(working, local, 0) < 0) { println(RED, "no active device"); return; }
    }
    disk_info();
}

static void block_column(const char *value, unsigned int width)
{
    print(WHITE, value);
    unsigned int length = strlen(value);
    do { print(WHITE, " "); } while (++length < width);
}
static void block_number(unsigned int value, unsigned int width)
{
    print_uint(WHITE, value);
    unsigned int length = 1;
    for (unsigned int n = value; n >= 10; n /= 10) length++;
    do { print(WHITE, " "); } while (++length < width);
}
/* Inspect on-disk superblocks without replacing the active ext2 image or cwd.
 * Unknown/unsupported statistics are shown as '-', never as zero free space. */
void fs_lsblk(void)
{
    DiskSelection previous = disk_selection();
    int count = disk_device_count();
    println(WHITE, "NAME    TYPE SIZE_KiB  FSTYPE  USED_KiB  FREE_KiB  MOUNTPOINT");
    for (int i = 0; i < count; i++) {
        const char *name = disk_device_name(i);
        if (disk_select_raw(name)) continue;
        DiskSelection physical = disk_selection();
        const char *type = disk_type();
        uint64_t capacity = disk_capacity();
        const char *filesystem = "-", *point = "-";
        unsigned int total = 0, free = 0;
        int have_usage = 0;
        unsigned char super[512];
        if (!disk_select(name) && !disk_read(2, super) && get16(super + 56) == 0xef53 &&
            get32(super + 76) <= 1 && (!get32(super + 76) ||
            (!get32(super + 92) && !(get32(super + 96) & ~2U) && !(get32(super + 100) & ~3U)))) {
            filesystem = "ext2";
            unsigned int shift = get32(super + 24);
            total = get32(super + 4); free = get32(super + 12);
            /* The physical capacity bounds multiplication and rejects corrupt
             * counts, including superblocks that extend past the device. */
            DiskSelection volume = disk_selection();
            unsigned int available = physical.sectors - volume.offset;
            if (shift <= 6 && total && free <= total && total <= (available >> (shift + 1))) {
                total <<= shift; free <<= shift; have_usage = 1;
            }
        }
        DiskSelection volume = disk_selection();
        if (!namespace_active) {
            if (mounted && previous.id == volume.id && previous.offset == volume.offset &&
                previous.generation == volume.generation) point = "/";
        } else {
            for (int m = 0; m < MOUNT_COUNT; m++)
                if (mounts[m].used && mounts[m].disk.id == volume.id &&
                    mounts[m].disk.offset == volume.offset &&
                    mounts[m].disk.generation == volume.generation) { point = mounts[m].path; break; }
        }
        block_column(name, 8); block_column(type, 5);
        char capacity_text[21]; disk_u64toa(capacity >> 1, capacity_text);
        block_column(capacity_text, 10); block_column(filesystem, 8);
        if (have_usage) { block_number(total - free, 10); block_number(free, 10); }
        else { block_column("-", 10); block_column("-", 10); }
        println(WHITE, point);
    }
    disk_restore(previous);
}
