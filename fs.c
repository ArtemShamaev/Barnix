#include "fs.h"
#include "barnix.h"
#include "disk.h"

/* Ext2, one block group, 1 KiB blocks. All offsets are on-disk byte offsets.
 * No C struct layout is serialized. The previous private format is not mounted.
 */
#define BLOCK_SIZE 1024U
#define MAX_BLOCKS 2048U
#define ROOT_INO 2U
#define FILE_MODE 0x8000
#define DIR_MODE 0x4000
#define MAX_FILE 255
#define MAX_NAME 23

static unsigned char image[MAX_BLOCKS * BLOCK_SIZE];
static unsigned char saved[MAX_BLOCKS * BLOCK_SIZE];
static unsigned int blocks, inode_count, inode_size, first_inode;
static unsigned int block_bitmap, inode_bitmap, inode_table, table_blocks;
static unsigned int cwd;
static int mounted, filetype;
#define SB (image + BLOCK_SIZE)
#define GD (image + 2 * BLOCK_SIZE)

static unsigned int get16(const unsigned char *p)
{ return p[0] | ((unsigned int)p[1] << 8); }
static unsigned int get32(const unsigned char *p)
{ return get16(p) | (get16(p + 2) << 16); }
static void put16(unsigned char *p, unsigned int n)
{ p[0] = n; p[1] = n >> 8; }
static void put32(unsigned char *p, unsigned int n)
{ put16(p, n); put16(p + 2, n >> 16); }
static int bit(const unsigned char *map, unsigned int n)
{ return (map[n / 8] >> (n % 8)) & 1; }
static void setbit(unsigned char *map, unsigned int n, int used)
{
    if (used) map[n / 8] |= 1U << (n % 8);
    else map[n / 8] &= ~(1U << (n % 8));
}
static int data_block(unsigned int b)
{
    return b > 2 && b < blocks && b != block_bitmap && b != inode_bitmap &&
           !(b >= inode_table && b < inode_table + table_blocks);
}
static unsigned char *inode(unsigned int n)
{
    if (!n || n > inode_count || !bit(image + inode_bitmap * BLOCK_SIZE, n - 1))
        return NULL;
    return image + inode_table * BLOCK_SIZE + (n - 1) * inode_size;
}
static int kind(const unsigned char *node) { return get16(node) & 0xF000; }
static int supported(unsigned char *node, int type)
{
    if (!node || kind(node) != type || get32(node + 32) || get32(node + 104) ||
        (type == FILE_MODE && (get32(node + 4) > MAX_FILE || get32(node + 108))))
        return 0;
    unsigned int size = get32(node + 4);
    if (type == DIR_MODE && (!size || size > 12 * BLOCK_SIZE || size % BLOCK_SIZE))
        return 0;
    for (int i = 0; i < 15; i++)
    {
        unsigned int b = get32(node + 40 + i * 4);
        if (b && (i >= 12 || !data_block(b) ||
                  !bit(image + block_bitmap * BLOCK_SIZE, b - 1))) return 0;
        if (type == FILE_MODE && i > 0 && b) return 0;
        if (type == DIR_MODE && (unsigned int)i < size / BLOCK_SIZE && !b) return 0;
    }
    return get16(node + 26) != 0;
}

/* Validate variable-length ext2 directory records before following them. */
static int record(unsigned char *entry, unsigned int remaining)
{
    if (remaining < 8) return 0;
    unsigned int length = get16(entry + 4), names = entry[6];
    return length >= 8 && !(length & 3) && length <= remaining &&
           names <= length - 8 && (filetype || entry[7] == 0) &&
           get32(entry) <= inode_count;
}
static int name_equal(unsigned char *e, const char *name)
{ return (int)e[6] == strlen(name) && strncmp((char *)e + 8, name, e[6]) == 0; }

/* Returns inode, -1 for absent name, -2 for an unsupported/corrupt directory. */
static int lookup(unsigned int dir, const char *name, unsigned char **found)
{
    unsigned char *node = inode(dir);
    if (!supported(node, DIR_MODE)) return -2;
    for (unsigned int i = 0; i < get32(node + 4) / BLOCK_SIZE; i++)
    {
        unsigned char *base = image + get32(node + 40 + i * 4) * BLOCK_SIZE;
        for (unsigned int off = 0; off < BLOCK_SIZE;)
        {
            unsigned char *e = base + off;
            if (!record(e, BLOCK_SIZE - off)) return -2;
            if (get32(e) && name_equal(e, name))
            {
                if (found) *found = e;
                return get32(e);
            }
            off += get16(e + 4);
        }
    }
    return -1;
}
static int valid_name(const char *name)
{
    int length = strlen(name);
    return length > 0 && length <= MAX_NAME && !strchr(name, '/') &&
           strcmp(name, ".") && strcmp(name, "..");
}
static void entry_set(unsigned char *e, unsigned int ino, unsigned int len,
                      const char *name, int type)
{
    memset(e, 0, len);
    put32(e, ino); put16(e + 4, len); e[6] = strlen(name);
    e[7] = filetype ? (type == DIR_MODE ? 2 : 1) : 0;
    memcpy(e + 8, name, e[6]);
}
static unsigned int allocate(int is_inode)
{
    unsigned char *map = image + (is_inode ? inode_bitmap : block_bitmap) * BLOCK_SIZE;
    unsigned int limit = is_inode ? inode_count : blocks - 1;
    unsigned int start = is_inode ? first_inode - 1 : 2;
    for (unsigned int i = start; i < limit; i++)
        if (!bit(map, i) && (is_inode || data_block(i + 1)))
        {
            unsigned char *sc = SB + (is_inode ? 16 : 12);
            unsigned char *gc = GD + (is_inode ? 14 : 12);
            if (!get32(sc) || !get16(gc)) return 0;
            setbit(map, i, 1);
            put32(sc, get32(sc) - 1); put16(gc, get16(gc) - 1);
            if (is_inode) memset(inode(i + 1), 0, inode_size);
            else memset(image + (i + 1) * BLOCK_SIZE, 0, BLOCK_SIZE);
            return i + 1;
        }
    return 0;
}
static void release(unsigned int n, int is_inode)
{
    unsigned char *map = image + (is_inode ? inode_bitmap : block_bitmap) * BLOCK_SIZE;
    unsigned char *sc = SB + (is_inode ? 16 : 12), *gc = GD + (is_inode ? 14 : 12);
    setbit(map, n - 1, 0);
    put32(sc, get32(sc) + 1); put16(gc, get16(gc) + 1);
}
static int insert(unsigned int dir, const char *name, unsigned int ino, int type)
{
    unsigned char *node = inode(dir);
    if (!supported(node, DIR_MODE) || lookup(dir, name, NULL) != -1) return -1;
    unsigned int need = (8 + strlen(name) + 3) & ~3U;
    unsigned int count = get32(node + 4) / BLOCK_SIZE;
    for (unsigned int i = 0; i < count; i++)
    {
        unsigned char *base = image + get32(node + 40 + i * 4) * BLOCK_SIZE;
        for (unsigned int off = 0; off < BLOCK_SIZE;)
        {
            unsigned char *e = base + off;
            if (!record(e, BLOCK_SIZE - off)) return -1;
            unsigned int len = get16(e + 4);
            unsigned int used = get32(e) ? (8 + e[6] + 3) & ~3U : 0;
            if (len - used >= need)
            {
                if (used) put16(e + 4, used);
                entry_set(e + used, ino, len - used, name, type);
                return 0;
            }
            off += len;
        }
    }
    if (count == 12) return -1;
    unsigned int b = allocate(0);
    if (!b) return -1;
    put32(node + 40 + count * 4, b);
    put32(node + 4, (count + 1) * BLOCK_SIZE);
    put32(node + 28, get32(node + 28) + 2);
    entry_set(image + b * BLOCK_SIZE, ino, BLOCK_SIZE, name, type);
    return 0;
}

/* Keep failed logical operations out of the disk image. I/O failure unmounts
 * the filesystem: ext2 has no journal, so a partial write needs host e2fsck. */
static int begin(void)
{
    if (!mounted) return -1;
    memcpy(saved, image, blocks * BLOCK_SIZE);
    return 0;
}
static int finish(int result)
{
    if (result != 0) { memcpy(image, saved, blocks * BLOCK_SIZE); return -1; }
    if (!memcmp(image, saved, blocks * BLOCK_SIZE)) return 0;
    /* Mark the volume unclean before the first metadata/data write, and clean
     * only after the complete operation. A reset mid-operation requires fsck. */
    put16(SB + 58, 2);
    if (disk_write(2, SB)) goto io_error;
    put16(SB + 58, 1);
    for (unsigned int sector = 0; sector < blocks * 2; sector++)
    {
        unsigned char *now = image + sector * 512;
        if (sector != 2 && memcmp(now, saved + sector * 512, 512) &&
            disk_write(sector, now) != 0) goto io_error;
    }
    if (disk_write(2, SB)) goto io_error;
    return 0;
io_error:
    mounted = 0;
    println(RED, "ext2 write failed; run e2fsck before remounting");
    return -1;
}

int fs_init(void)
{
    mounted = 0;
    if (disk_read(2, SB) || disk_read(3, SB + 512)) return -1;
    unsigned int revision = get32(SB + 76);
    blocks = get32(SB + 4); inode_count = get32(SB);
    inode_size = revision ? get16(SB + 88) : 128;
    first_inode = revision ? get32(SB + 84) : 11;
    if (get16(SB + 56) != 0xEF53 || revision > 1 || get16(SB + 58) != 1 ||
        get32(SB + 24) != 0 || get32(SB + 28) != 0 || get32(SB + 20) != 1 ||
        blocks < 16 || blocks > MAX_BLOCKS || !inode_count || inode_count > 8192 ||
        get32(SB + 32) < blocks - 1 || get32(SB + 32) > 8192 ||
        get32(SB + 36) != get32(SB + 32) ||
        get32(SB + 40) != inode_count ||
        (inode_size != 128 && inode_size != 256) ||
        first_inode < 11 || first_inode > inode_count ||
        (revision && (get32(SB + 92) || (get32(SB + 96) & ~2U) ||
                      (get32(SB + 100) & ~3U))))
    {
        println(RED, "Unsupported ext2: use the documented 1 KiB, one-group profile");
        return -1;
    }
    filetype = revision && (get32(SB + 96) & 2);
    for (unsigned int s = 0; s < blocks * 2; s++)
        if (disk_read(s, image + s * 512)) return -1;
    block_bitmap = get32(GD); inode_bitmap = get32(GD + 4); inode_table = get32(GD + 8);
    table_blocks = (inode_count * inode_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    if (block_bitmap <= 2 || block_bitmap >= blocks || inode_bitmap <= 2 ||
        inode_bitmap >= blocks || block_bitmap == inode_bitmap || inode_table <= 2 ||
        inode_table >= blocks || table_blocks > blocks - inode_table ||
        (block_bitmap >= inode_table && block_bitmap < inode_table + table_blocks) ||
        (inode_bitmap >= inode_table && inode_bitmap < inode_table + table_blocks) ||
        get32(SB + 12) != get16(GD + 12) || get32(SB + 16) != get16(GD + 14)) return -1;
    /* Every metadata block and reserved inode must already be allocated. */
    for (unsigned int b = 1; b < blocks; b++)
        if (!data_block(b) && !bit(image + block_bitmap * BLOCK_SIZE, b - 1)) return -1;
    for (unsigned int n = 1; n < first_inode; n++)
        if (!bit(image + inode_bitmap * BLOCK_SIZE, n - 1)) return -1;
    if (!supported(inode(ROOT_INO), DIR_MODE) ||
        lookup(ROOT_INO, ".", NULL) != ROOT_INO || lookup(ROOT_INO, "..", NULL) != ROOT_INO)
        return -1;
    cwd = ROOT_INO; mounted = 1;
    return 0;
}

static int create(const char *name, int type)
{
    if (!valid_name(name) || lookup(cwd, name, NULL) != -1) return -1;
    unsigned int n = allocate(1);
    if (!n) return -1;
    unsigned char *node = inode(n);
    put16(node, type | (type == DIR_MODE ? 0755 : 0644));
    put16(node + 26, type == DIR_MODE ? 2 : 1);
    /* Until Barnix has a clock, inherit the volume's last-write timestamp. */
    put32(node + 8, get32(SB + 48)); put32(node + 12, get32(SB + 48));
    put32(node + 16, get32(SB + 48));
    if (type == DIR_MODE)
    {
        unsigned int b = allocate(0);
        if (!b) return -1;
        put32(node + 4, BLOCK_SIZE); put32(node + 28, 2); put32(node + 40, b);
        entry_set(image + b * BLOCK_SIZE, n, 12, ".", DIR_MODE);
        entry_set(image + b * BLOCK_SIZE + 12, cwd, BLOCK_SIZE - 12, "..", DIR_MODE);
        unsigned char *parent = inode(cwd);
        if (get16(parent + 26) == 65535) return -1;
        put16(parent + 26, get16(parent + 26) + 1);
        put16(GD + 16, get16(GD + 16) + 1);
    }
    return insert(cwd, name, n, type);
}
int fs_touch(const char *name)
{ if (begin()) return -1; return finish(create(name, FILE_MODE)); }
int fs_mkdir(const char *name)
{ if (begin()) return -1; return finish(create(name, DIR_MODE)); }

static int write_file(const char *name, const char *data, int size)
{
    if (size < 0 || size > MAX_FILE || !valid_name(name)) return -1;
    int n = lookup(cwd, name, NULL);
    if (n == -1)
    {
        if (create(name, FILE_MODE)) return -1;
        n = lookup(cwd, name, NULL);
    }
    unsigned char *node = n > 0 ? inode(n) : NULL;
    if (!supported(node, FILE_MODE)) return -1;
    unsigned int b = get32(node + 40);
    if (size && !b)
    {
        b = allocate(0); if (!b) return -1;
        put32(node + 40, b); put32(node + 28, 2);
    }
    if (b)
    {
        memset(image + b * BLOCK_SIZE, 0, BLOCK_SIZE);
        memcpy(image + b * BLOCK_SIZE, data, size);
    }
    put32(node + 4, size);
    return 0;
}
int fs_write(const char *name, const char *data, int size)
{ if (begin()) return -1; return finish(write_file(name, data, size)); }
static int read_file(const char *name, char *buffer)
{
    if (!mounted) return -1;
    int n = lookup(cwd, name, NULL);
    unsigned char *node = n > 0 ? inode(n) : NULL;
    if (!supported(node, FILE_MODE)) return -1;
    unsigned int size = get32(node + 4), b = get32(node + 40);
    memset(buffer, 0, MAX_FILE + 1);
    if (b) memcpy(buffer, image + b * BLOCK_SIZE, size);
    return size;
}
void fs_cat(const char *name)
{
    char buffer[MAX_FILE + 1];
    if (read_file(name, buffer) < 0) println(RED, "file missing or unsupported");
    else println(WHITE, buffer);
}
int fs_append(const char *name, const char *data, int size)
{
    char buffer[MAX_FILE + 1];
    if (!mounted || size < 0 || size > MAX_FILE) return -1;
    if (lookup(cwd, name, NULL) == -1) return fs_write(name, data, size);
    int old = read_file(name, buffer);
    if (old < 0 || size > MAX_FILE - old) return -1;
    memcpy(buffer + old, data, size);
    return fs_write(name, buffer, old + size);
}
int fs_cp(const char *source, const char *destination)
{
    char buffer[MAX_FILE + 1];
    int size = read_file(source, buffer);
    if (size < 0 || lookup(cwd, destination, NULL) != -1) return -1;
    return fs_write(destination, buffer, size);
}
int fs_mv(const char *source, const char *destination)
{
    if (begin()) return -1;
    unsigned char *e = NULL;
    int n = lookup(cwd, source, &e);
    unsigned char *node = n > 0 ? inode(n) : NULL;
    if (!valid_name(source) || !valid_name(destination) || !node ||
        (!supported(node, FILE_MODE) && !supported(node, DIR_MODE)) ||
        lookup(cwd, destination, NULL) != -1) return finish(-1);
    put32(e, 0);
    return finish(insert(cwd, destination, n, kind(node)));
}
static int remove_node(const char *name, int type)
{
    unsigned char *e = NULL;
    int n = lookup(cwd, name, &e);
    unsigned char *node = n > 0 ? inode(n) : NULL;
    if (!valid_name(name) || n < (int)first_inode || !supported(node, type)) return -1;
    if (type == DIR_MODE)
    {
        for (unsigned int i = 0; i < get32(node + 4) / BLOCK_SIZE; i++)
        {
            unsigned char *base = image + get32(node + 40 + i * 4) * BLOCK_SIZE;
            for (unsigned int off = 0; off < BLOCK_SIZE;)
            {
                unsigned char *child = base + off;
                if (!record(child, BLOCK_SIZE - off)) return -1;
                if (get32(child) && !name_equal(child, ".") && !name_equal(child, "..")) return -1;
                off += get16(child + 4);
            }
        }
        if (get16(node + 26) != 2) return -1;
        unsigned char *parent = inode(cwd);
        put16(parent + 26, get16(parent + 26) - 1);
        put16(GD + 16, get16(GD + 16) - 1);
    }
    put32(e, 0);
    unsigned int links = type == DIR_MODE ? 0 : get16(node + 26) - 1;
    put16(node + 26, links);
    if (!links)
    {
        for (int i = 0; i < 12; i++)
        {
            unsigned int b = get32(node + 40 + i * 4);
            if (b) release(b, 0);
        }
        unsigned int deleted_at = get32(SB + 48);
        /* Small dtime values encode an ext3 orphan link, not a timestamp. */
        if (deleted_at <= inode_count) deleted_at = inode_count + 1;
        memset(node, 0, inode_size); put32(node + 20, deleted_at);
        release(n, 1);
    }
    return 0;
}
int fs_rm(const char *name)
{ if (begin()) return -1; return finish(remove_node(name, FILE_MODE)); }
int fs_rmdir(const char *name)
{ if (begin()) return -1; return finish(remove_node(name, DIR_MODE)); }
int fs_cd(const char *name)
{
    if (!mounted) return -1;
    int n = strcmp(name, "/") == 0 ? (int)ROOT_INO : lookup(cwd, name, NULL);
    if (n <= 0 || !supported(inode(n), DIR_MODE)) return -1;
    cwd = n; return 0;
}
void fs_ls(void)
{
    if (!mounted) return;
    unsigned char *node = inode(cwd);
    if (!supported(node, DIR_MODE)) return;
    for (unsigned int i = 0; i < get32(node + 4) / BLOCK_SIZE; i++)
    {
        unsigned char *base = image + get32(node + 40 + i * 4) * BLOCK_SIZE;
        for (unsigned int off = 0; off < BLOCK_SIZE;)
        {
            unsigned char *e = base + off;
            if (!record(e, BLOCK_SIZE - off)) return;
            if (get32(e) && !name_equal(e, ".") && !name_equal(e, ".."))
            {
                char name[256]; memcpy(name, e + 8, e[6]); name[e[6]] = 0;
                unsigned char *child = inode(get32(e));
                print(WHITE, name);
                if (child && kind(child) == DIR_MODE) print(CYAN, "/");
                println(WHITE, "");
            }
            off += get16(e + 4);
        }
    }
}
void fs_pwd(void)
{
    unsigned int path[64], count = 0, n = cwd;
    if (!mounted) return;
    while (n != ROOT_INO)
    {
        if (count == 64) { println(RED, "directory depth limit"); return; }
        path[count++] = n;
        int parent = lookup(n, "..", NULL);
        if (parent <= 0 || (unsigned int)parent == n) return;
        n = parent;
    }
    print(WHITE, "/");
    while (count)
    {
        unsigned int child = path[--count];
        unsigned char *node = inode(n);
        if (!supported(node, DIR_MODE)) return;
        int found = 0;
        for (unsigned int i = 0; i < get32(node + 4) / BLOCK_SIZE && !found; i++)
        {
            unsigned char *base = image + get32(node + 40 + i * 4) * BLOCK_SIZE;
            for (unsigned int off = 0; off < BLOCK_SIZE;)
            {
                unsigned char *e = base + off;
                if (!record(e, BLOCK_SIZE - off)) return;
                if (get32(e) == child && !name_equal(e, ".") && !name_equal(e, ".."))
                {
                    char name[256]; memcpy(name, e + 8, e[6]); name[e[6]] = 0;
                    print(WHITE, name); found = 1; break;
                }
                off += get16(e + 4);
            }
        }
        if (!found) return;
        if (count) print(WHITE, "/");
        n = child;
    }
    println(WHITE, "");
}
int fs_stat(const char *name)
{
    if (!mounted) return -1;
    int n = lookup(cwd, name, NULL);
    unsigned char *node = n > 0 ? inode(n) : NULL;
    if (!node) return -1;
    print(WHITE, "Name: "); println(WHITE, name);
    print(WHITE, "Type: "); println(WHITE, kind(node) == DIR_MODE ? "directory" :
                                          kind(node) == FILE_MODE ? "file" : "other");
    print(WHITE, "Bytes: "); print_uint(WHITE, get32(node + 4)); println(WHITE, "");
    return 0;
}
void fs_df(void)
{
    if (!mounted) return;
    println(WHITE, "ext2 (1 KiB blocks)");
    print(WHITE, "Free inodes: "); print_uint(WHITE, get32(SB + 16));
    print(WHITE, " / "); print_uint(WHITE, inode_count);
    print(WHITE, "   Free blocks: "); print_uint(WHITE, get32(SB + 12));
    print(WHITE, " / "); print_uint(WHITE, blocks); println(WHITE, "");
    println(WHITE, "Barnix limits: 255 bytes/file, 23 chars/new name");
}
int fs_sync(void)
{
    /* Each successful operation has already flushed all changed sectors. */
    if (!mounted) { println(RED, "ext2 is not mounted"); return -1; }
    return 0;
}
