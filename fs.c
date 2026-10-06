#include "lang.h"
#include "fs.h"
int engine_init(void);
int engine_exec_check(const char *name);
int engine_size(const char *name);
int engine_read(const char *name, unsigned int offset, void *buffer, unsigned int size);
void engine_ls(void);
void engine_cat(const char *name);
int engine_touch(const char *name);
int engine_write(const char *name, const char *data, int size);
int engine_rm(const char *name);
int engine_mkdir(const char *name);
int engine_cd(const char *name);
void engine_pwd(void);
int engine_getcwd(char *out, unsigned int capacity);
void engine_df(void);
int engine_stat(const char *name);
int engine_append(const char *name, const char *data, int size);
int engine_cp(const char *source, const char *destination);
int engine_mv(const char *source, const char *destination);
int engine_rmdir(const char *name);
int engine_sync(void);
int engine_is_dir(const char *name);
int engine_getdents(const char *name, void *buffer, unsigned int capacity, unsigned int *position);
#include "barnix.h"
#include "disk.h"

/* Ext2, multiple block groups, 1 KiB blocks. All offsets are on-disk byte offsets.
 * No C struct layout is serialized. The previous private format is not mounted.
 */
#define BLOCK_SIZE 1024U
#define MAX_BLOCKS FS_MAX_BLOCKS
#define MAX_GROUPS 640U
#define CACHE_BLOCKS 2048U
#define ROOT_INO 2U
#define FILE_MODE 0x8000
#define DIR_MODE 0x4000
#define MAX_FILE FS_MAX_FILE_SIZE
#define MAX_NAME 23

/* The cache pins blocks for one public operation, so pointers stay valid.
 * Each operation fits the current 268 KiB/file limit without loading a volume. */
static unsigned char superblock[BLOCK_SIZE], descriptors[MAX_GROUPS * 32];
static unsigned char saved_super[BLOCK_SIZE], saved_descriptors[MAX_GROUPS * 32];
static struct { unsigned int block; unsigned char data[BLOCK_SIZE], before[BLOCK_SIZE]; } cache[CACHE_BLOCKS];
static unsigned int cache_count;
static int cache_error;
static unsigned char failed_block[BLOCK_SIZE];
static unsigned int blocks, inode_count, inode_size, first_inode;
static unsigned int groups, blocks_per_group, inodes_per_group, descriptor_blocks;
static unsigned int block_bitmap, inode_bitmap, inode_table, table_blocks;
static unsigned int cwd, allocation_hint[2];
static int mounted, filetype;
#define SB superblock
#define GD descriptors
static void cache_reset(void) { cache_count = 0; cache_error = 0; }
static unsigned char *block(unsigned int n)
{
    for (unsigned int i = 0; i < cache_count; i++) if (cache[i].block == n) return cache[i].data;
    if (n >= blocks || cache_count == CACHE_BLOCKS || disk_read_many(n * 2, failed_block, 2)) {
        cache_error = 1; memset(failed_block, 0, sizeof(failed_block)); return failed_block;
    }
    unsigned int slot = cache_count++;
    cache[slot].block = n;
    memcpy(cache[slot].data, failed_block, BLOCK_SIZE);
    memcpy(cache[slot].before, failed_block, BLOCK_SIZE);
    return cache[slot].data;
}

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
static unsigned char *group_desc(unsigned int g) { return GD + g * 32; }
static int power_of(unsigned int n, unsigned int base)
{ while (n > 1 && n % base == 0) n /= base; return n == 1; }
static int backup_group(unsigned int g)
{
    return !(get32(SB + 100) & 1) || g == 0 || g == 1 ||
           power_of(g, 3) || power_of(g, 5) || power_of(g, 7);
}
static unsigned int group_start(unsigned int g) { return 1 + g * blocks_per_group; }
static unsigned int group_size(unsigned int g)
{
    unsigned int remaining = blocks - group_start(g);
    return remaining < blocks_per_group ? remaining : blocks_per_group;
}
static int data_block(unsigned int b)
{
    if (!b || b >= blocks) return 0;
    unsigned int g = (b - 1) / blocks_per_group;
    unsigned char *desc = group_desc(g);
    unsigned int table = get32(desc + 8);
    return !(backup_group(g) && b < group_start(g) + 1 + descriptor_blocks) &&
           b != get32(desc) && b != get32(desc + 4) &&
           !(b >= table && b < table + table_blocks);
}
static unsigned char *inode(unsigned int n)
{
    if (!n || n > inode_count) return NULL;
    unsigned int g = (n - 1) / inodes_per_group, index = (n - 1) % inodes_per_group;
    unsigned char *desc = group_desc(g);
    if (!bit(block(get32(desc + 4)), index)) return NULL;
    unsigned int offset = index * inode_size;
    return block(get32(desc + 8) + offset / BLOCK_SIZE) + offset % BLOCK_SIZE;
}
static int kind(const unsigned char *node) { return get16(node) & 0xF000; }
static int engine_ready(void)
{
    if (mounted && !disk_present())
    {
        mounted = 0;
        println(RED, tr("device removed; filesystem unmounted"));
    }
    return mounted;
}

/* Files use 12 direct pointers and one singly indirect block (256 pointers). */
static unsigned int file_block(unsigned char *node, unsigned int index)
{
    if (index < 12) return get32(node + 40 + index * 4);
    unsigned int indirect = get32(node + 88);
    return indirect ? get32(block(indirect) + (index - 12) * 4) : 0;
}
static int mark_block(unsigned int *seen, unsigned int count, unsigned int b)
{
    if (!data_block(b)) return 0;
    unsigned int g = (b - 1) / blocks_per_group;
    if (!bit(block(get32(group_desc(g))), b - group_start(g))) return 0;
    for (unsigned int i = 0; i < count; i++) if (seen[i] == b) return 0;
    seen[count] = b;
    return !cache_error;
}
static int supported(unsigned char *node, int type)
{
    if (!node || kind(node) != type || get32(node + 32) || get32(node + 104) ||
        (type == FILE_MODE && (get32(node + 4) > MAX_FILE || get32(node + 108))) ||
        get32(node + 92) || get32(node + 96)) return 0;
    unsigned int size = get32(node + 4), count = 0;
    if (type == DIR_MODE && (!size || size > 12 * BLOCK_SIZE || size % BLOCK_SIZE ||
                            get32(node + 88))) return 0;
    unsigned int seen[269];
    memset(seen, 0, sizeof(seen));
    unsigned int indirect = get32(node + 88);
    if (indirect)
    {
        if (!mark_block(seen, count, indirect)) return 0;
        count++;
    }
    unsigned int limit = type == FILE_MODE ? 268 : 12;
    for (unsigned int i = 0; i < limit; i++)
    {
        unsigned int b = file_block(node, i);
        if (b)
        {
            if (!mark_block(seen, count, b)) return 0;
            count++;
        }
        if (type == DIR_MODE && i < size / BLOCK_SIZE && !b) return 0;
    }
    return get16(node + 26) != 0 && get32(node + 28) == count * 2;
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
static int lookup(unsigned int dir, const char *name, unsigned char **found);

/* Resolve a simple absolute or relative path.  This is intentionally small,
 * but is enough for ELF programs stored below /bin. */
static int lookup_path(const char *path)
{
    if (!path || !*path) return -1;
    unsigned int dir = path[0] == '/' ? ROOT_INO : cwd;
    const char *p = path;
    while (*p == '/') p++;
    if (!*p) return dir;
    char part[MAX_NAME + 1];
    while (*p)
    {
        unsigned int n = 0;
        while (*p && *p != '/')
        {
            if (n == MAX_NAME) return -1;
            part[n++] = *p++;
        }
        part[n] = 0;
        int child;
        if (!strcmp(part, ".")) child = dir;
        else if (!strcmp(part, "..")) child = lookup(dir, "..", NULL);
        else child = lookup(dir, part, NULL);
        if (child <= 0) return -1;
        dir = child;
        while (*p == '/') p++;
    }
    return dir;
}

/* Returns inode, -1 for absent name, -2 for an unsupported/corrupt directory. */
static int lookup(unsigned int dir, const char *name, unsigned char **found)
{
    unsigned char *node = inode(dir);
    if (!supported(node, DIR_MODE)) return -2;
    for (unsigned int i = 0; i < get32(node + 4) / BLOCK_SIZE; i++)
    {
        unsigned char *base = block(get32(node + 40 + i * 4));
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
    for (unsigned int step = 0; step < groups; step++) {
        unsigned int g = (allocation_hint[is_inode] + step) % groups;
        unsigned char *desc = group_desc(g), *sc = SB + (is_inode ? 16 : 12);
        unsigned char *gc = desc + (is_inode ? 14 : 12);
        if (!get16(gc)) continue;
        unsigned char *map = block(get32(desc + (is_inode ? 4 : 0)));
        unsigned int limit = is_inode ? inodes_per_group : group_size(g);
        unsigned int start = is_inode && !g ? first_inode - 1 : 0;
        for (unsigned int i = start; i < limit; i++) if (!bit(map, i)) {
            unsigned int n = is_inode ? g * inodes_per_group + i + 1 : group_start(g) + i;
            if ((!is_inode && !data_block(n)) || !get32(sc) || cache_error) return 0;
            setbit(map, i, 1); put32(sc, get32(sc) - 1); put16(gc, get16(gc) - 1);
            if (is_inode) memset(inode(n), 0, inode_size);
            else memset(block(n), 0, BLOCK_SIZE);
            allocation_hint[is_inode] = g;
            return cache_error ? 0 : n;
        }
        return 0; /* A free-count/bitmap mismatch is corruption, not ENOSPC. */
    }
    return 0;
}
static void release(unsigned int n, int is_inode)
{
    unsigned int g = is_inode ? (n - 1) / inodes_per_group : (n - 1) / blocks_per_group;
    unsigned int index = is_inode ? (n - 1) % inodes_per_group : n - group_start(g);
    unsigned char *desc = group_desc(g), *map = block(get32(desc + (is_inode ? 4 : 0)));
    unsigned char *sc = SB + (is_inode ? 16 : 12), *gc = desc + (is_inode ? 14 : 12);
    setbit(map, index, 0);
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
        unsigned char *base = block(get32(node + 40 + i * 4));
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
    entry_set(block(b), ino, BLOCK_SIZE, name, type);
    return 0;
}

/* Keep failed logical operations out of the disk image. I/O failure unmounts
 * the filesystem: ext2 has no journal, so a partial write needs host e2fsck. */
static int begin(void)
{
    if (!engine_ready() || cache_error) return -1;
    memcpy(saved_super, SB, BLOCK_SIZE);
    memcpy(saved_descriptors, GD, sizeof(descriptors));
    return 0;
}
static int finish(int result)
{
    if (result || cache_error) {
        memcpy(SB, saved_super, BLOCK_SIZE); memcpy(GD, saved_descriptors, sizeof(descriptors));
        for (unsigned int i = 0; i < cache_count; i++) memcpy(cache[i].data, cache[i].before, BLOCK_SIZE);
        return -1;
    }
    int changed = memcmp(SB, saved_super, BLOCK_SIZE) || memcmp(GD, saved_descriptors, groups * 32);
    for (unsigned int i = 0; i < cache_count && !changed; i++)
        changed = memcmp(cache[i].data, cache[i].before, BLOCK_SIZE);
    if (!changed) return 0;
    put16(SB + 58, 2);
    if (disk_write(2, SB) || disk_flush()) goto io_error;
    put16(SB + 58, 1);
    for (unsigned int i = 0; i < cache_count; i++) for (unsigned int sector = 0; sector < 2; sector++) {
        unsigned int offset = sector * 512;
        if (memcmp(cache[i].data + offset, cache[i].before + offset, 512) &&
            disk_write(cache[i].block * 2 + sector, cache[i].data + offset)) goto io_error;
    }
    for (unsigned int s = 0; s < descriptor_blocks * 2; s++)
        if (memcmp(GD + s * 512, saved_descriptors + s * 512, 512) && disk_write(4 + s, GD + s * 512)) goto io_error;
    if (memcmp(SB + 512, saved_super + 512, 512) && disk_write(3, SB + 512)) goto io_error;
    if (disk_flush() || disk_write(2, SB) || disk_flush()) goto io_error;
    return 0;
io_error:
    mounted = 0;
    println(RED, tr("ext2 write failed; run e2fsck before remounting"));
    return -1;
}

int engine_init(void)
{
    mounted = 0;
    if (disk_read(2, SB) || disk_read(3, SB + 512)) return -1;
    unsigned int revision = get32(SB + 76);
    blocks = get32(SB + 4); inode_count = get32(SB);
    inode_size = revision ? get16(SB + 88) : 128;
    first_inode = revision ? get32(SB + 84) : 11;
    blocks_per_group = get32(SB + 32); inodes_per_group = get32(SB + 40);
    if (get16(SB + 56) != 0xef53 || revision > 1 || get16(SB + 58) != 1 ||
        get32(SB + 24) || get32(SB + 28) || get32(SB + 20) != 1 ||
        blocks < 16 || blocks > MAX_BLOCKS || blocks > disk_selection().sectors / 2 ||
        !blocks_per_group || blocks_per_group > 8192 || get32(SB + 36) != blocks_per_group ||
        !inodes_per_group || inodes_per_group > 8192 ||
        (inode_size != 128 && inode_size != 256) || (inodes_per_group * inode_size) % BLOCK_SIZE ||
        first_inode < 11 || first_inode > inode_count ||
        (revision && (get32(SB + 92) || (get32(SB + 96) & ~2U) || (get32(SB + 100) & ~3U)))) {
        println(RED, tr("Unsupported ext2: requires 1 KiB blocks and at most 5 GiB")); return -1;
    }
    groups = (blocks - 2) / blocks_per_group + 1;
    if (groups > MAX_GROUPS || inode_count != groups * inodes_per_group) return -1;
    descriptor_blocks = (groups * 32 + BLOCK_SIZE - 1) / BLOCK_SIZE;
    table_blocks = inodes_per_group * inode_size / BLOCK_SIZE;
    memset(GD, 0, sizeof(descriptors));
    for (unsigned int s = 0; s < descriptor_blocks * 2; s++) if (disk_read(4 + s, GD + s * 512)) return -1;
    filetype = revision && (get32(SB + 96) & 2);
    unsigned int free_blocks = 0, free_inodes = 0;
    for (unsigned int g = 0; g < groups; g++) {
        unsigned char *d = group_desc(g);
        unsigned int first = group_start(g), end = first + group_size(g);
        unsigned int bitmap = get32(d), imap = get32(d + 4), table = get32(d + 8);
        unsigned int metadata_end = first + (backup_group(g) ? 1 + descriptor_blocks : 0);
        if (metadata_end > end || bitmap < metadata_end || bitmap >= end || imap < metadata_end || imap >= end ||
            bitmap == imap || table < metadata_end || table >= end || table_blocks > end - table ||
            (bitmap >= table && bitmap < table + table_blocks) || (imap >= table && imap < table + table_blocks) ||
            get16(d + 12) > group_size(g) || get16(d + 14) > inodes_per_group) return -1;
        free_blocks += get16(d + 12); free_inodes += get16(d + 14);
        /* Validate only metadata bits; data blocks remain on disk until read. */
        unsigned char map[BLOCK_SIZE];
        if (disk_read_many(bitmap * 2, map, 2)) return -1;
        for (unsigned int b = first; b < metadata_end; b++) if (!bit(map, b - first)) return -1;
        if (!bit(map, bitmap - first) || !bit(map, imap - first)) return -1;
        for (unsigned int b = table; b < table + table_blocks; b++) if (!bit(map, b - first)) return -1;
    }
    if (get32(SB + 12) != free_blocks || get32(SB + 16) != free_inodes) return -1;
    block_bitmap = get32(GD); inode_bitmap = get32(GD + 4); inode_table = get32(GD + 8);
    for (unsigned int n = 1; n < first_inode; n++) if (!bit(block(inode_bitmap), n - 1)) return -1;
    if (!supported(inode(ROOT_INO), DIR_MODE) || lookup(ROOT_INO, ".", NULL) != ROOT_INO ||
        lookup(ROOT_INO, "..", NULL) != ROOT_INO || cache_error) return -1;
    allocation_hint[0] = allocation_hint[1] = 0;
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
        entry_set(block(b), n, 12, ".", DIR_MODE);
        entry_set(block(b) + 12, cwd, BLOCK_SIZE - 12, "..", DIR_MODE);
        unsigned char *parent = inode(cwd);
        if (get16(parent + 26) == 65535) return -1;
        put16(parent + 26, get16(parent + 26) + 1);
        unsigned char *d = group_desc((n - 1) / inodes_per_group);
        put16(d + 16, get16(d + 16) + 1);
    }
    return insert(cwd, name, n, type);
}
int engine_touch(const char *name)
{ if (begin()) return -1; return finish(create(name, FILE_MODE)); }
int engine_mkdir(const char *name)
{ if (begin()) return -1; return finish(create(name, DIR_MODE)); }

static unsigned char *file_slot(unsigned char *node, unsigned int index, int create_block)
{
    if (index < 12) return node + 40 + index * 4;
    unsigned int indirect = get32(node + 88);
    if (!indirect && create_block)
    {
        indirect = allocate(0);
        if (!indirect) return NULL;
        put32(node + 88, indirect);
        put32(node + 28, get32(node + 28) + 2);
    }
    return indirect ? block(indirect) + (index - 12) * 4 : NULL;
}
static void truncate_blocks(unsigned char *node, unsigned int keep)
{
    for (unsigned int i = keep; i < 268; i++)
    {
        unsigned char *slot = file_slot(node, i, 0);
        if (slot && get32(slot))
        {
            release(get32(slot), 0); put32(slot, 0);
            put32(node + 28, get32(node + 28) - 2);
        }
    }
    if (keep <= 12 && get32(node + 88))
    {
        release(get32(node + 88), 0); put32(node + 88, 0);
        put32(node + 28, get32(node + 28) - 2);
    }
}
static int write_range(unsigned char *node, unsigned int offset, const char *data,
                       unsigned int size)
{
    while (size)
    {
        unsigned int index = offset / BLOCK_SIZE, within = offset % BLOCK_SIZE;
        unsigned int chunk = BLOCK_SIZE - within;
        if (chunk > size) chunk = size;
        unsigned char *slot = file_slot(node, index, 1);
        if (!slot) return -1;
        unsigned int b = get32(slot);
        if (!b)
        {
            b = allocate(0); if (!b) return -1;
            put32(slot, b); put32(node + 28, get32(node + 28) + 2);
        }
        memcpy(block(b) + within, data, chunk);
        offset += chunk; data += chunk; size -= chunk;
    }
    if (offset > get32(node + 4)) put32(node + 4, offset);
    return 0;
}
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
    truncate_blocks(node, (size + BLOCK_SIZE - 1) / BLOCK_SIZE);
    if (write_range(node, 0, data, size)) return -1;
    if (size % BLOCK_SIZE)
    {
        unsigned int b = file_block(node, size / BLOCK_SIZE);
        memset(block(b) + size % BLOCK_SIZE, 0, BLOCK_SIZE - size % BLOCK_SIZE);
    }
    put32(node + 4, size);
    return 0;
}
int engine_write(const char *name, const char *data, int size)
{
    if (!name || begin()) return -1;
    /* Resolve the parent without changing the caller's working directory. */
    unsigned int previous = cwd;
    const char *base = name, *slash = NULL;
    for (const char *p = name; *p; p++) if (*p == '/') slash = p;
    if (slash) {
        char parent[256];
        unsigned int n = slash - name;
        if (n >= sizeof(parent)) return finish(-1);
        if (!n) { parent[0] = '/'; n = 1; }
        else memcpy(parent, name, n);
        parent[n] = 0;
        int dir = lookup_path(parent);
        if (dir <= 0 || !supported(inode(dir), DIR_MODE)) return finish(-1);
        cwd = dir; base = slash + 1;
    }
    int result = write_file(base, data, size);
    cwd = previous;
    return finish(result);
}
int engine_size(const char *name)
{
    if (!engine_ready()) return -1;
    int n = lookup_path(name);
    unsigned char *node = n > 0 ? inode(n) : NULL;
    return supported(node, FILE_MODE) ? (int)get32(node + 4) : -1;
}
int engine_exec_check(const char *name)
{
    if (!engine_ready()) return -5;
    int n = lookup_path(name);
    if (n < 0) return -2;
    unsigned char *node = inode(n);
    if (!node) return -5;
    if (kind(node) != FILE_MODE || !(get16(node) & 0111)) return -13;
    return supported(node, FILE_MODE) ? 0 : -8;
}
int engine_is_dir(const char *name)
{
    if (!engine_ready()) return 0;
    int n = lookup_path(name); return n > 0 && supported(inode(n), DIR_MODE);
}
int engine_getdents(const char *name, void *buffer, unsigned int capacity, unsigned int *position)
{
    if (!engine_ready() || !buffer || !position) return -1;
    int n = lookup_path(name); unsigned char *node = n > 0 ? inode(n) : NULL;
    if (!supported(node, DIR_MODE)) return -1;
    unsigned int index = *position, written = 0, current = 0;
    for (unsigned int i = 0; i < get32(node + 4) / BLOCK_SIZE; i++) {
        unsigned char *base = block(get32(node + 40 + i * 4));
        for (unsigned int off = 0; off < BLOCK_SIZE;) {
            unsigned char *e = base + off; if (!record(e, BLOCK_SIZE - off)) return -1;
            if (get32(e) && current++ >= index) {
                unsigned int name_len = e[6], reclen = (10 + name_len + 2 + 3) & ~3U;
                if (reclen > capacity - written) { *position = index; return written ? (int)written : -22; }
                unsigned char *out = (unsigned char *)buffer + written; memset(out, 0, reclen);
                put32(out, get32(e)); put32(out + 4, current); put16(out + 8, reclen);
                unsigned char *child = inode(get32(e));
                out[reclen - 1] = child && kind(child) == DIR_MODE ? 4 :
                                  child && kind(child) == FILE_MODE ? 8 : 0;
                memcpy(out + 10, e + 8, name_len); out[10 + name_len] = 0; written += reclen; index++;
            }
            off += get16(e + 4);
        }
    }
    *position = index; return (int)written;
}
int engine_read(const char *name, unsigned int offset, void *buffer, unsigned int size)
{
    if (!engine_ready()) return -1;
    int n = lookup_path(name);
    unsigned char *node = n > 0 ? inode(n) : NULL;
    if (!supported(node, FILE_MODE) || offset > get32(node + 4)) return -1;
    if (size > get32(node + 4) - offset) size = get32(node + 4) - offset;
    unsigned int result = size;
    unsigned char *out = buffer;
    while (size)
    {
        unsigned int b = file_block(node, offset / BLOCK_SIZE);
        unsigned int within = offset % BLOCK_SIZE, chunk = BLOCK_SIZE - within;
        if (chunk > size) chunk = size;
        if (b) memcpy(out, block(b) + within, chunk);
        else memset(out, 0, chunk); /* ext2 sparse-file holes */
        out += chunk; offset += chunk; size -= chunk;
    }
    return result;
}
void engine_cat(const char *name)
{
    int size = engine_size(name);
    if (size < 0) { println(RED, tr("file missing or unsupported")); return; }
    char buffer[257];
    for (int offset = 0; offset < size;)
    {
        int count = engine_read(name, offset, buffer, sizeof(buffer) - 1);
        if (count <= 0) { println(RED, tr("read failed")); return; }
        buffer[count] = 0; print(WHITE, buffer); offset += count;
    }
    println(WHITE, "");
}
int engine_append(const char *name, const char *data, int size)
{
    if (!engine_ready() || size < 0 || size > MAX_FILE) return -1;
    int n = lookup(cwd, name, NULL);
    if (n == -1) return engine_write(name, data, size);
    unsigned char *node = n > 0 ? inode(n) : NULL;
    if (!supported(node, FILE_MODE) || (unsigned int)size > MAX_FILE - get32(node + 4))
        return -1;
    if (begin()) return -1;
    return finish(write_range(node, get32(node + 4), data, size));
}
int engine_cp(const char *source, const char *destination)
{
    static char buffer[MAX_FILE];
    int size = engine_size(source);
    if (size < 0 || lookup(cwd, destination, NULL) != -1 ||
        engine_read(source, 0, buffer, size) != size) return -1;
    return engine_write(destination, buffer, size);
}

int engine_mv(const char *source, const char *destination)
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
            unsigned char *base = block(get32(node + 40 + i * 4));
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
        unsigned char *d = group_desc((n - 1) / inodes_per_group);
        put16(d + 16, get16(d + 16) - 1);
    }
    put32(e, 0);
    unsigned int links = type == DIR_MODE ? 0 : get16(node + 26) - 1;
    put16(node + 26, links);
    if (!links)
    {
        truncate_blocks(node, 0);
        unsigned int deleted_at = get32(SB + 48);
        /* Small dtime values encode an ext3 orphan link, not a timestamp. */
        if (deleted_at <= inode_count) deleted_at = inode_count + 1;
        memset(node, 0, inode_size); put32(node + 20, deleted_at);
        release(n, 1);
    }
    return 0;
}
int engine_rm(const char *name)
{ if (begin()) return -1; return finish(remove_node(name, FILE_MODE)); }
int engine_rmdir(const char *name)
{ if (begin()) return -1; return finish(remove_node(name, DIR_MODE)); }
int engine_cd(const char *name)
{
    if (!engine_ready()) return -1;
    int n = lookup_path(name);
    if (n <= 0 || !supported(inode(n), DIR_MODE)) return -1;
    cwd = n; return 0;
}
void engine_ls(void)
{
    if (!engine_ready()) return;
    unsigned char *node = inode(cwd);
    if (!supported(node, DIR_MODE)) return;
    for (unsigned int i = 0; i < get32(node + 4) / BLOCK_SIZE; i++)
    {
        unsigned char *base = block(get32(node + 40 + i * 4));
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
int engine_getcwd(char *out, unsigned int capacity)
{
    unsigned int path[64], count = 0, n = cwd;
    unsigned int length = 0;
    if (!out || capacity < 2) return -34;
    if (!engine_ready()) return -5;
    while (n != ROOT_INO)
    {
        if (count == 64) return -36;
        path[count++] = n;
        int parent = lookup(n, "..", NULL);
        if (parent <= 0 || (unsigned int)parent == n) return -5;
        n = parent;
    }
    out[length++] = '/';
    while (count)
    {
        unsigned int child = path[--count];
        unsigned char *node = inode(n);
        if (!supported(node, DIR_MODE)) return -5;
        int found = 0;
        for (unsigned int i = 0; i < get32(node + 4) / BLOCK_SIZE && !found; i++)
        {
            unsigned char *base = block(get32(node + 40 + i * 4));
            for (unsigned int off = 0; off < BLOCK_SIZE;)
            {
                unsigned char *e = base + off;
                if (!record(e, BLOCK_SIZE - off)) return -5;
                if (get32(e) == child && !name_equal(e, ".") && !name_equal(e, ".."))
                {
                    if (e[6] >= capacity - length) return -34;
                    memcpy(out + length, e + 8, e[6]); length += e[6];
                    found = 1; break;
                }
                off += get16(e + 4);
            }
        }
        if (!found) return -5;
        if (count) {
            if (length + 1 >= capacity) return -34;
            out[length++] = '/';
        }
        n = child;
    }
    out[length] = 0;
    return length + 1;
}
void engine_pwd(void)
{
    unsigned int path[64], count = 0, n = cwd;
    if (!engine_ready()) return;
    while (n != ROOT_INO)
    {
        if (count == 64) { println(RED, tr("directory depth limit")); return; }
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
            unsigned char *base = block(get32(node + 40 + i * 4));
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
int engine_stat(const char *name)
{
    if (!engine_ready()) return -1;
    int n = lookup_path(name);
    unsigned char *node = n > 0 ? inode(n) : NULL;
    if (!node) return -1;
    print(WHITE, tr("Name: ")); println(WHITE, name);
    print(WHITE, tr("Type: ")); println(WHITE, kind(node) == DIR_MODE ? tr("directory") :
                                          kind(node) == FILE_MODE ? tr("file") : tr("other"));
    print(WHITE, tr("Bytes: ")); print_uint(WHITE, get32(node + 4)); println(WHITE, "");
    return 0;
}
void engine_df(void)
{
    if (!engine_ready()) return;
    println(WHITE, tr("ext2 (1 KiB blocks)"));
    print(WHITE, tr("Free inodes: ")); print_uint(WHITE, get32(SB + 16));
    print(WHITE, " / "); print_uint(WHITE, inode_count);
    print(WHITE, tr("   Free blocks: ")); print_uint(WHITE, get32(SB + 12));
    print(WHITE, " / "); print_uint(WHITE, blocks); println(WHITE, "");
    println(WHITE, tr("Barnix limits: 268 KiB/file, 23 chars/new name"));
}
int engine_sync(void)
{
    /* Each successful operation has already flushed all changed sectors. */
    if (!engine_ready()) { println(RED, tr("ext2 is not mounted")); return -1; }
    if (disk_flush()) { println(RED, tr("device flush failed")); return -1; }
    return 0;
}
int ext_init(void)
{ cache_reset(); int result = engine_init(); if (cache_error) { mounted = 0; return -1; } return result; }
int ext_exec_check(const char *name)
{ cache_reset(); int result = engine_exec_check(name); if (cache_error) { mounted = 0; return -1; } return result; }
int ext_size(const char *name)
{ cache_reset(); int result = engine_size(name); if (cache_error) { mounted = 0; return -1; } return result; }
int ext_read(const char *name, unsigned int offset, void *buffer, unsigned int size)
{ cache_reset(); int result = engine_read(name, offset, buffer, size); if (cache_error) { mounted = 0; return -1; } return result; }
void ext_ls(void)
{ cache_reset(); engine_ls(); if (cache_error) mounted = 0; }
void ext_cat(const char *name)
{ cache_reset(); engine_cat(name); if (cache_error) mounted = 0; }
int ext_touch(const char *name)
{ cache_reset(); int result = engine_touch(name); if (cache_error) { mounted = 0; return -1; } return result; }
int ext_write(const char *name, const char *data, int size)
{ cache_reset(); int result = engine_write(name, data, size); if (cache_error) { mounted = 0; return -1; } return result; }
int ext_rm(const char *name)
{ cache_reset(); int result = engine_rm(name); if (cache_error) { mounted = 0; return -1; } return result; }
int ext_mkdir(const char *name)
{ cache_reset(); int result = engine_mkdir(name); if (cache_error) { mounted = 0; return -1; } return result; }
int ext_cd(const char *name)
{ cache_reset(); int result = engine_cd(name); if (cache_error) { mounted = 0; return -1; } return result; }
void ext_pwd(void)
{ cache_reset(); engine_pwd(); if (cache_error) mounted = 0; }
int ext_getcwd(char *out, unsigned int capacity)
{ cache_reset(); int result = engine_getcwd(out, capacity); if (cache_error) { mounted = 0; return -1; } return result; }
void ext_df(void)
{ cache_reset(); engine_df(); if (cache_error) mounted = 0; }
int ext_stat(const char *name)
{ cache_reset(); int result = engine_stat(name); if (cache_error) { mounted = 0; return -1; } return result; }
int ext_append(const char *name, const char *data, int size)
{ cache_reset(); int result = engine_append(name, data, size); if (cache_error) { mounted = 0; return -1; } return result; }
int ext_cp(const char *source, const char *destination)
{ cache_reset(); int result = engine_cp(source, destination); if (cache_error) { mounted = 0; return -1; } return result; }
int ext_mv(const char *source, const char *destination)
{ cache_reset(); int result = engine_mv(source, destination); if (cache_error) { mounted = 0; return -1; } return result; }
int ext_rmdir(const char *name)
{ cache_reset(); int result = engine_rmdir(name); if (cache_error) { mounted = 0; return -1; } return result; }
int ext_sync(void)
{ cache_reset(); int result = engine_sync(); if (cache_error) { mounted = 0; return -1; } return result; }
int ext_is_dir(const char *name)
{ cache_reset(); int result = engine_is_dir(name); if (cache_error) { mounted = 0; return 0; } return result; }
int ext_getdents(const char *name, void *buffer, unsigned int capacity, unsigned int *position)
{ cache_reset(); int result = engine_getdents(name, buffer, capacity, position); if (cache_error) { mounted = 0; return -1; } return result; }
#include "ext2_format.inc"
#include "ext2_import.inc"
#include "permissions.h"
#include "vfs.inc"
#include "permissions.c"
#include "partition.c"
#include "fdisk.inc"
