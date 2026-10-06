/* Install from the bounded, single-group ext2 template embedded in Setup.
 * The destination is already formatted and selected. No source writes occur. */
static const unsigned char *import_image;
static unsigned int import_bytes, import_inode_size, import_inodes, import_table;
static unsigned char import_data[MAX_FILE];
static unsigned char import_verify[4096];
static const unsigned char import_zero[BLOCK_SIZE];
static const unsigned char *import_block(unsigned int n)
{
    if (!n) return import_zero; /* ext2 sparse file hole */
    if (n >= import_bytes / BLOCK_SIZE) return NULL;
    return import_image + n * BLOCK_SIZE;
}
static const unsigned char *import_inode(unsigned int n)
{
    if (!n || n > import_inodes) return NULL;
    unsigned int off = import_table * BLOCK_SIZE + (n - 1) * import_inode_size;
    if (off > import_bytes || import_inode_size > import_bytes - off) return NULL;
    return import_image + off;
}
static const unsigned char *import_file_block(const unsigned char *node, unsigned int index)
{
    if (index < 12) return import_block(get32(node + 40 + index * 4));
    if (index >= 268) return NULL;
    const unsigned char *indirect = import_block(get32(node + 88));
    return indirect ? import_block(get32(indirect + (index - 12) * 4)) : NULL;
}
static int import_mode(const char *name, unsigned int mode)
{
    cache_reset();
    if (begin()) return -1;
    int n = lookup_path(name);
    if (n <= 0) return finish(-1);
    put16(inode(n), mode);
    return finish(0);
}
static int import_directory(unsigned int ino, unsigned int depth)
{
    const unsigned char *node = import_inode(ino);
    if (!node || kind(node) != DIR_MODE || depth > 16) return -1;
    unsigned int size = get32(node + 4);
    if (size > 12 * BLOCK_SIZE || size % BLOCK_SIZE) return -1;
    for (unsigned int pos = 0; pos < size; pos += BLOCK_SIZE) {
        const unsigned char *data = import_file_block(node, pos / BLOCK_SIZE);
        if (!data) return -1;
        for (unsigned int off = 0; off < BLOCK_SIZE;) {
            const unsigned char *entry = data + off;
            unsigned int length = get16(entry + 4), n = get32(entry), namesize = entry[6];
            if (length < 8 || length % 4 || length > BLOCK_SIZE - off || namesize > length - 8) return -1;
            off += length;
            if (!n || (namesize == 1 && entry[8] == '.') ||
                (namesize == 2 && entry[8] == '.' && entry[9] == '.')) continue;
            if (!namesize || namesize > 23) return -1;
            char name[24]; memcpy(name, entry + 8, namesize); name[namesize] = 0;
            if (!valid_name(name)) return -1;
            const unsigned char *child = import_inode(n);
            if (!child) return -1;
            if (kind(child) == DIR_MODE) {
                if (ext_mkdir(name) || ext_cd(name) || import_directory(n, depth + 1) || ext_cd("..")) return -1;
            } else if (kind(child) == FILE_MODE) {
                unsigned int bytes = get32(child + 4);
                if (bytes > MAX_FILE) return -1;
                for (unsigned int p = 0; p < bytes; p += BLOCK_SIZE) {
                    const unsigned char *chunk = import_file_block(child, p / BLOCK_SIZE);
                    if (!chunk) return -1;
                    unsigned int count = bytes - p; if (count > BLOCK_SIZE) count = BLOCK_SIZE;
                    memcpy(import_data + p, chunk, count);
                }
                if (ext_write(name, (const char *)import_data, bytes)) return -1;
                for (unsigned int p = 0; p < bytes; p += sizeof(import_verify)) {
                    unsigned int count = bytes - p; if (count > sizeof(import_verify)) count = sizeof(import_verify);
                    if (ext_read(name, p, import_verify, count) != (int)count || memcmp(import_verify, import_data + p, count)) return -1;
                }
            } else return -1;
            if (import_mode(name, get16(child))) return -1;
        }
    }
    return 0;
}
int fs_import_template(const void *image, unsigned int bytes)
{
    if (!image || bytes != 2 * 1024 * 1024) return -1;
    import_image = image; import_bytes = bytes;
    const unsigned char *sb = import_image + 1024;
    if (get16(sb + 56) != 0xef53 || get32(sb + 24) || get32(sb + 4) > bytes / BLOCK_SIZE ||
        get32(sb + 32) < get32(sb + 4) - 1 || get32(sb + 96) & ~2U) return -1;
    import_inode_size = get32(sb + 76) ? get16(sb + 88) : 128;
    import_inodes = get32(sb);
    import_table = get32(import_image + 2048 + 8);
    if ((import_inode_size != 128 && import_inode_size != 256) || import_inodes > 8192 ||
        !import_inodes || import_table >= bytes / BLOCK_SIZE || !import_inode(import_inodes)) return -1;
    if (ext_init()) return -1;
    return import_directory(ROOT_INO, 0);
}
