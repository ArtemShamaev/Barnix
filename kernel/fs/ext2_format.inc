/* Format only metadata, never allocate or zero a multi-GiB image in RAM. */
static unsigned char format_buffer[BLOCK_SIZE];
static int write_block(unsigned int number, const unsigned char *data)
{ return disk_write(number * 2, data) || disk_write(number * 2 + 1, data + 512) ? -1 : 0; }
int fs_format_selected(void)
{
    DiskSelection target = disk_selection();
    if (target.sectors < 4096) return -1;
    mounted = 0; cache_reset();
    blocks = target.sectors / 2;
    if (blocks > MAX_BLOCKS) blocks = MAX_BLOCKS;
    blocks_per_group = 8192;
    inodes_per_group = 128;
    inode_size = 128; first_inode = 11; filetype = 2;
    table_blocks = inodes_per_group * inode_size / BLOCK_SIZE;
    groups = (blocks - 2) / blocks_per_group + 1;
    descriptor_blocks = (groups * 32 + BLOCK_SIZE - 1) / BLOCK_SIZE;
    memset(SB, 0, BLOCK_SIZE); memset(GD, 0, sizeof(descriptors));
    put32(SB + 100, 1); /* sparse_super */
    while (groups > 1 && group_size(groups - 1) < table_blocks + 3 + descriptor_blocks) {
        blocks = group_start(groups - 1); groups--;
        descriptor_blocks = (groups * 32 + BLOCK_SIZE - 1) / BLOCK_SIZE;
    }
    inode_count = groups * inodes_per_group;
    put32(SB, inode_count); put32(SB + 4, blocks);
    put32(SB + 16, inode_count - 10); put32(SB + 20, 1);
    put32(SB + 32, blocks_per_group); put32(SB + 36, blocks_per_group);
    put32(SB + 40, inodes_per_group); put16(SB + 54, 0xffff);
    put16(SB + 56, 0xef53); put16(SB + 58, 2); put16(SB + 60, 1);
    put32(SB + 76, 1); put32(SB + 84, first_inode); put16(SB + 88, inode_size); put32(SB + 96, 2);
    unsigned int total_free = 0;
    for (unsigned int g = 0; g < groups; g++) {
        unsigned char *d = group_desc(g);
        unsigned int first = group_start(g), reserve = backup_group(g) ? 1 + descriptor_blocks : 0;
        put32(d, first + reserve); put32(d + 4, first + reserve + 1); put32(d + 8, first + reserve + 2);
        unsigned int used = reserve + 2 + table_blocks + (g == 0);
        put16(d + 12, group_size(g) - used); put16(d + 14, inodes_per_group - (g ? 0 : 10));
        put16(d + 16, g ? 0 : 1); total_free += group_size(g) - used;
    }
    put32(SB + 12, total_free);
    block_bitmap = get32(GD); inode_bitmap = get32(GD + 4); inode_table = get32(GD + 8);
    unsigned int root_block = inode_table + table_blocks;
    /* Mark unclean before replacing any metadata. Keep it dirty on failure. */
    if (write_block(1, SB) || disk_flush()) return -1;
    memset(format_buffer, 0, BLOCK_SIZE);
    if (write_block(0, format_buffer)) return -1;
    for (unsigned int g = 0; g < groups; g++) {
        unsigned char *d = group_desc(g);
        memset(format_buffer, 0, BLOCK_SIZE);
        unsigned int used = group_size(g) - get16(d + 12);
        for (unsigned int bit_index = 0; bit_index < 8192; bit_index++)
            if (bit_index < used || bit_index >= group_size(g)) setbit(format_buffer, bit_index, 1);
        if (write_block(get32(d), format_buffer)) return -1;
        memset(format_buffer, 0, BLOCK_SIZE);
        for (unsigned int bit_index = 0; bit_index < 8192; bit_index++)
            if ((!g && bit_index < 10) || bit_index >= inodes_per_group) setbit(format_buffer, bit_index, 1);
        if (write_block(get32(d + 4), format_buffer)) return -1;
        for (unsigned int b = 0; b < table_blocks; b++) {
            memset(format_buffer, 0, BLOCK_SIZE);
            if (!g && !b) {
                unsigned char *root = format_buffer + inode_size;
                put16(root, DIR_MODE | 0755); put32(root + 4, BLOCK_SIZE);
                put16(root + 26, 2); put32(root + 28, 2); put32(root + 40, root_block);
            }
            if (write_block(get32(d + 8) + b, format_buffer)) return -1;
        }
    }
    memset(format_buffer, 0, BLOCK_SIZE);
    entry_set(format_buffer, ROOT_INO, 12, ".", DIR_MODE);
    entry_set(format_buffer + 12, ROOT_INO, BLOCK_SIZE - 12, "..", DIR_MODE);
    if (write_block(root_block, format_buffer)) return -1;
    for (unsigned int g = 0; g < groups; g++) if (backup_group(g)) {
        for (unsigned int b = 0; b < descriptor_blocks; b++)
            if (write_block(group_start(g) + 1 + b, GD + b * BLOCK_SIZE)) return -1;
        if (g) {
            memcpy(format_buffer, SB, BLOCK_SIZE); put16(format_buffer + 58, 1); put16(format_buffer + 90, g);
            if (write_block(group_start(g), format_buffer)) return -1;
        }
    }
    if (disk_flush()) return -1;
    put16(SB + 58, 1);
    return write_block(1, SB) || disk_flush() ? -1 : 0;
}
