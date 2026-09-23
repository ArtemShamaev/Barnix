#include "lang.h"
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
#define MAX_FILE FS_MAX_FILE_SIZE
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
static int fs_ready(void)
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
    return indirect ? get32(image + indirect * BLOCK_SIZE + (index - 12) * 4) : 0;
}
static int mark_block(unsigned char *seen, unsigned int b)
{
    if (!data_block(b) || !bit(image + block_bitmap * BLOCK_SIZE, b - 1) || bit(seen, b))
        return 0;
    setbit(seen, b, 1);
    return 1;
}
static int supported(unsigned char *node, int type)
{
    if (!node || kind(node) != type || get32(node + 32) || get32(node + 104) ||
        (type == FILE_MODE && (get32(node + 4) > MAX_FILE || get32(node + 108))) ||
        get32(node + 92) || get32(node + 96)) return 0;
    unsigned int size = get32(node + 4), count = 0;
    if (type == DIR_MODE && (!size || size > 12 * BLOCK_SIZE || size % BLOCK_SIZE ||
                            get32(node + 88))) return 0;
    unsigned char seen[MAX_BLOCKS / 8];
    memset(seen, 0, sizeof(seen));
    unsigned int indirect = get32(node + 88);
    if (indirect)
    {
        if (!mark_block(seen, indirect)) return 0;
        count++;
    }
    unsigned int limit = type == FILE_MODE ? 268 : 12;
    for (unsigned int i = 0; i < limit; i++)
    {
        unsigned int b = file_block(node, i);
        if (b)
        {
            if (!mark_block(seen, b)) return 0;
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
    if (!fs_ready()) return -1;
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
    if (disk_write(2, SB) || disk_flush()) goto io_error;
    put16(SB + 58, 1);
    for (unsigned int sector = 0; sector < blocks * 2; sector++)
    {
        unsigned char *now = image + sector * 512;
        if (sector != 2 && memcmp(now, saved + sector * 512, 512) &&
            disk_write(sector, now) != 0) goto io_error;
    }
    if (disk_flush() || disk_write(2, SB) || disk_flush()) goto io_error;
    return 0;
io_error:
    mounted = 0;
    println(RED, tr("ext2 write failed; run e2fsck before remounting"));
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
        println(RED, tr("Unsupported ext2: use the documented 1 KiB, one-group profile"));
        return -1;
    }
    filetype = revision && (get32(SB + 96) & 2);
    for (unsigned int s = 0; s < blocks * 2;)
    {
        unsigned int count = blocks * 2 - s;
        if (count > 8) count = 8;
        if (disk_read_many(s, image + s * 512, count)) return -1;
        s += count;
    }
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
    return indirect ? image + indirect * BLOCK_SIZE + (index - 12) * 4 : NULL;
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
        memcpy(image + b * BLOCK_SIZE + within, data, chunk);
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
        memset(image + b * BLOCK_SIZE + size % BLOCK_SIZE, 0, BLOCK_SIZE - size % BLOCK_SIZE);
    }
    put32(node + 4, size);
    return 0;
}
int fs_write(const char *name, const char *data, int size)
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
int fs_size(const char *name)
{
    if (!fs_ready()) return -1;
    int n = lookup_path(name);
    unsigned char *node = n > 0 ? inode(n) : NULL;
    return supported(node, FILE_MODE) ? (int)get32(node + 4) : -1;
}
int fs_exec_check(const char *name)
{
    if (!fs_ready()) return -5;
    int n = lookup_path(name);
    if (n < 0) return -2;
    unsigned char *node = inode(n);
    if (!node) return -5;
    if (kind(node) != FILE_MODE || !(get16(node) & 0111)) return -13;
    return supported(node, FILE_MODE) ? 0 : -8;
}
int fs_is_dir(const char *name)
{
    if (!fs_ready()) return 0;
    int n = lookup_path(name); return n > 0 && supported(inode(n), DIR_MODE);
}
int fs_getdents(const char *name, void *buffer, unsigned int capacity, unsigned int *position)
{
    if (!fs_ready() || !buffer || !position) return -1;
    int n = lookup_path(name); unsigned char *node = n > 0 ? inode(n) : NULL;
    if (!supported(node, DIR_MODE)) return -1;
    unsigned int index = *position, written = 0, current = 0;
    for (unsigned int i = 0; i < get32(node + 4) / BLOCK_SIZE; i++) {
        unsigned char *base = image + get32(node + 40 + i * 4) * BLOCK_SIZE;
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
int fs_read(const char *name, unsigned int offset, void *buffer, unsigned int size)
{
    if (!fs_ready()) return -1;
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
        if (b) memcpy(out, image + b * BLOCK_SIZE + within, chunk);
        else memset(out, 0, chunk); /* ext2 sparse-file holes */
        out += chunk; offset += chunk; size -= chunk;
    }
    return result;
}
void fs_cat(const char *name)
{
    int size = fs_size(name);
    if (size < 0) { println(RED, tr("file missing or unsupported")); return; }
    char buffer[257];
    for (int offset = 0; offset < size;)
    {
        int count = fs_read(name, offset, buffer, sizeof(buffer) - 1);
        if (count <= 0) { println(RED, tr("read failed")); return; }
        buffer[count] = 0; print(WHITE, buffer); offset += count;
    }
    println(WHITE, "");
}
int fs_append(const char *name, const char *data, int size)
{
    if (!fs_ready() || size < 0 || size > MAX_FILE) return -1;
    int n = lookup(cwd, name, NULL);
    if (n == -1) return fs_write(name, data, size);
    unsigned char *node = n > 0 ? inode(n) : NULL;
    if (!supported(node, FILE_MODE) || (unsigned int)size > MAX_FILE - get32(node + 4))
        return -1;
    if (begin()) return -1;
    return finish(write_range(node, get32(node + 4), data, size));
}
int fs_cp(const char *source, const char *destination)
{
    static char buffer[MAX_FILE];
    int size = fs_size(source);
    if (size < 0 || lookup(cwd, destination, NULL) != -1 ||
        fs_read(source, 0, buffer, size) != size) return -1;
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
        truncate_blocks(node, 0);
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
    if (!fs_ready()) return -1;
    int n = lookup_path(name);
    if (n <= 0 || !supported(inode(n), DIR_MODE)) return -1;
    cwd = n; return 0;
}
void fs_ls(void)
{
    if (!fs_ready()) return;
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
int fs_getcwd(char *out, unsigned int capacity)
{
    unsigned int path[64], count = 0, n = cwd;
    unsigned int length = 0;
    if (!out || capacity < 2) return -34;
    if (!fs_ready()) return -5;
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
            unsigned char *base = image + get32(node + 40 + i * 4) * BLOCK_SIZE;
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
void fs_pwd(void)
{
    unsigned int path[64], count = 0, n = cwd;
    if (!fs_ready()) return;
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
    if (!fs_ready()) return -1;
    int n = lookup(cwd, name, NULL);
    unsigned char *node = n > 0 ? inode(n) : NULL;
    if (!node) return -1;
    print(WHITE, tr("Name: ")); println(WHITE, name);
    print(WHITE, tr("Type: ")); println(WHITE, kind(node) == DIR_MODE ? tr("directory") :
                                          kind(node) == FILE_MODE ? tr("file") : tr("other"));
    print(WHITE, tr("Bytes: ")); print_uint(WHITE, get32(node + 4)); println(WHITE, "");
    return 0;
}
void fs_df(void)
{
    if (!fs_ready()) return;
    println(WHITE, tr("ext2 (1 KiB blocks)"));
    print(WHITE, tr("Free inodes: ")); print_uint(WHITE, get32(SB + 16));
    print(WHITE, " / "); print_uint(WHITE, inode_count);
    print(WHITE, tr("   Free blocks: ")); print_uint(WHITE, get32(SB + 12));
    print(WHITE, " / "); print_uint(WHITE, blocks); println(WHITE, "");
    println(WHITE, tr("Barnix limits: 268 KiB/file, 23 chars/new name"));
}
int fs_sync(void)
{
    /* Each successful operation has already flushed all changed sectors. */
    if (!fs_ready()) { println(RED, tr("ext2 is not mounted")); return -1; }
    if (disk_flush()) { println(RED, tr("device flush failed")); return -1; }
    return 0;
}
int fs_mount(const char *device, const char *mountpoint)
{
    if (!mountpoint || strcmp(mountpoint, "/") != 0)
        return -1;
    int previous_mounted = fs_ready();
    if (previous_mounted && fs_sync()) return -1;
    DiskSelection previous = disk_selection();
    unsigned int previous_cwd = cwd;
    if (disk_select(device)) return -1;
    if (fs_init() == 0) return 0;
    disk_restore(previous);
    if (previous_mounted && fs_init() == 0) cwd = previous_cwd;
    return -1;
}
int fs_unmount(void)
{
    if (fs_sync()) return -1;
    mounted = 0; cwd = ROOT_INO; disk_deselect();
    return 0;
}
void fs_mount_info(void)
{
    if (!fs_ready()) { println(WHITE, tr("no filesystem mounted")); return; }
    print(WHITE, tr("mounted ")); print(WHITE, disk_name()); println(WHITE, tr(" on / (ext2)"));
}
