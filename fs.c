#include "fs.h"
#include "barnix.h"
#include "disk.h"

#define EXT2_MAGIC 0xEF53
#define FS_MAX_INODES 64
#define FS_MAX_NAME 24
#define FS_MAX_DATA 256
#define FS_ROOT_INODE 0

typedef enum
{
    FS_FREE = 0,
    FS_FILE = 1,
    FS_DIR = 2
} FsNodeType;

typedef struct
{
    unsigned short magic;
    unsigned int inodes_count;
    unsigned int blocks_count;
    unsigned int free_inodes_count;
    unsigned int free_blocks_count;
    unsigned int first_data_block;
    unsigned int log_block_size;
} Ext2Superblock;

typedef struct
{
    FsNodeType type;
    int parent;
    int used;
    int size;
    char name[FS_MAX_NAME];
    char data[FS_MAX_DATA];
} FsInode;

static Ext2Superblock superblock;
static FsInode inodes[FS_MAX_INODES];
static int cwd;

static int valid_name(const char *name)
{
    int len = strlen(name);

    return len > 0 && len < FS_MAX_NAME && strcmp(name, ".") != 0 && strcmp(name, "..") != 0;
}

static int find_child(int parent, const char *name)
{
    for (int i = 0; i < FS_MAX_INODES; i++)
        if (inodes[i].used && inodes[i].parent == parent && strcmp(inodes[i].name, name) == 0)
            return i;

    return -1;
}

static int alloc_inode(void)
{
    for (int i = 1; i < FS_MAX_INODES; i++)
    {
        if (!inodes[i].used)
        {
            memset(&inodes[i], 0, sizeof(FsInode));
            inodes[i].used = 1;
            superblock.free_inodes_count--;
            return i;
        }
    }

    return -1;
}

static void sync_metadata(void)
{
    unsigned char sector[DISK_SECTOR_SIZE];
    memset(sector, 0, sizeof(sector));
    memcpy(sector, &superblock, sizeof(superblock));
    disk_write(2, sector);
}

static int create_node(const char *name, FsNodeType type)
{
    if (!valid_name(name))
    {
        println(RED, "bad name");
        return -1;
    }

    if (find_child(cwd, name) >= 0)
    {
        println(RED, "already exists");
        return -1;
    }

    int ino = alloc_inode();
    if (ino < 0)
    {
        println(RED, "no free inodes");
        return -1;
    }

    inodes[ino].type = type;
    inodes[ino].parent = cwd;
    strncpy(inodes[ino].name, name, FS_MAX_NAME - 1);
    inodes[ino].name[FS_MAX_NAME - 1] = 0;
    sync_metadata();
    return 0;
}

int fs_init(void)
{
    memset(&superblock, 0, sizeof(superblock));
    memset(inodes, 0, sizeof(inodes));

    superblock.magic = EXT2_MAGIC;
    superblock.inodes_count = FS_MAX_INODES;
    superblock.blocks_count = 4096;
    superblock.free_inodes_count = FS_MAX_INODES - 1;
    superblock.free_blocks_count = 4096 - 3;
    superblock.first_data_block = 3;
    superblock.log_block_size = 0;

    inodes[FS_ROOT_INODE].used = 1;
    inodes[FS_ROOT_INODE].type = FS_DIR;
    inodes[FS_ROOT_INODE].parent = FS_ROOT_INODE;
    strcpy(inodes[FS_ROOT_INODE].name, "/");
    cwd = FS_ROOT_INODE;

    sync_metadata();
    return 0;
}

void fs_ls(void)
{
    for (int i = 0; i < FS_MAX_INODES; i++)
    {
        if (inodes[i].used && inodes[i].parent == cwd && i != cwd)
        {
            print(inodes[i].type == FS_DIR ? CYAN : WHITE, inodes[i].name);
            if (inodes[i].type == FS_DIR)
                print(CYAN, "/");
            println(WHITE, "");
        }
    }
}

void fs_cat(const char *name)
{
    int ino = find_child(cwd, name);
    if (ino < 0 || inodes[ino].type != FS_FILE)
    {
        println(RED, "file not found");
        return;
    }

    println(WHITE, inodes[ino].data);
}

int fs_touch(const char *name)
{
    return create_node(name, FS_FILE);
}

int fs_mkdir(const char *name)
{
    return create_node(name, FS_DIR);
}

int fs_cd(const char *name)
{
    if (strcmp(name, "/") == 0)
    {
        cwd = FS_ROOT_INODE;
        return 0;
    }

    if (strcmp(name, "..") == 0)
    {
        cwd = inodes[cwd].parent;
        return 0;
    }

    int ino = find_child(cwd, name);
    if (ino < 0 || inodes[ino].type != FS_DIR)
        return -1;

    cwd = ino;
    return 0;
}

int fs_write(const char *name, const char *data, int size)
{
    int ino = find_child(cwd, name);
    if (ino < 0)
    {
        if (fs_touch(name) != 0)
            return -1;
        ino = find_child(cwd, name);
    }

    if (ino < 0 || inodes[ino].type != FS_FILE)
    {
        println(RED, "file not found");
        return -1;
    }

    if (size >= FS_MAX_DATA)
        size = FS_MAX_DATA - 1;

    memcpy(inodes[ino].data, data, size);
    inodes[ino].data[size] = 0;
    inodes[ino].size = size;
    sync_metadata();
    return 0;
}

int fs_rm(const char *name)
{
    int ino = find_child(cwd, name);
    if (ino < 0 || inodes[ino].type != FS_FILE)
    {
        println(RED, "file not found");
        return -1;
    }

    memset(&inodes[ino], 0, sizeof(FsInode));
    superblock.free_inodes_count++;
    sync_metadata();
    return 0;
}
