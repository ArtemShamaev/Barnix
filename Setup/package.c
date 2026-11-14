#include "package.h"
#include "sha256.h"
#include "barnix.h"
#include "fs.h"
#include "net.h"

enum { MAGIC, HEADER, PATH, BODY, COMPLETE };
#define PACKAGE_CHUNK_SIZE (32U * 1024U * 1024U)
typedef struct {
    SetupSha256 hash;
    int phase;
    unsigned int used, wanted, mode, size, written, total, processed;
    SetupPackageProgress progress;
    void *progress_context;
    unsigned char field[10];
    char path[256];
} Package;

static unsigned int get32(const unsigned char *bytes)
{
    return bytes[0] | (unsigned int)bytes[1] << 8 | (unsigned int)bytes[2] << 16 | (unsigned int)bytes[3] << 24;
}
static int valid_path(const char *path)
{
    if (*path++ != '/') return 0;
    int part = 0;
    for (; *path; path++) {
        if (*path == '/') {
            if (!part) return 0;
            part = 0;
        } else {
            if (!((*path >= 'a' && *path <= 'z') || (*path >= 'A' && *path <= 'Z') ||
                  (*path >= '0' && *path <= '9') || *path == '_' || *path == '-' ||
                  *path == '+' || *path == '.')) return 0;
            if (*path == '.' && (path[1] == '/' || !path[1]) &&
                (part == 0 || (part == 1 && path[-1] == '.'))) return 0;
            part++;
        }
    }
    return part > 0;
}
static int open_file(Package *package)
{
    if (!valid_path(package->path)) return -1;
    for (char *part = package->path + 1; *part; part++) if (*part == '/') {
        *part = 0;
        if (!fs_is_dir(package->path) && fs_mkdir(package->path)) return -1;
        *part = '/';
    }
    if (fs_file_size(package->path) >= 0) return -1;
    return fs_write(package->path, "", 0);
}
static int package_chunk(const void *data, unsigned int length, void *context)
{
    Package *package = context;
    unsigned int input_length = length;
    const unsigned char *bytes = data;
    setup_sha256_update(&package->hash, bytes, length);
    while (length) {
        if (package->phase == COMPLETE) return -1;
        if (package->phase == BODY) {
            unsigned int take = package->size - package->written;
            if (take > length) take = length;
            if (take && fs_pwrite(package->path, package->written, bytes, take, 0) < 0) return -1;
            package->written += take; bytes += take; length -= take;
            if (package->written == package->size) {
                if (fs_chmod(package->path, package->mode)) return -1;
                package->phase = HEADER; package->used = 0; package->wanted = 10;
            }
            continue;
        }
        unsigned int take = package->wanted - package->used;
        if (take > length) take = length;
        unsigned char *output = package->phase == PATH ? (unsigned char *)package->path : package->field;
        memcpy(output + package->used, bytes, take);
        package->used += take; bytes += take; length -= take;
        if (package->used != package->wanted) continue;
        if (package->phase == MAGIC) {
            if (memcmp(package->field, "BPK1", 4)) return -1;
            package->phase = HEADER; package->wanted = 10; package->used = 0;
        } else if (package->phase == HEADER) {
            unsigned int name_length = package->field[0] | (unsigned int)package->field[1] << 8;
            package->mode = get32(package->field + 2);
            package->size = get32(package->field + 6);
            if (!name_length) {
                if (package->mode || package->size) return -1;
                package->phase = COMPLETE;
                continue;
            }
            if (name_length > 255 || package->size > FS_MAX_FILE_SIZE ||
                (package->mode != 0100644 && package->mode != 0100755)) return -1;
            package->phase = PATH; package->wanted = name_length; package->used = 0;
        } else if (package->phase == PATH) {
            package->path[package->wanted] = 0;
            if (open_file(package)) return -1;
            package->written = 0;
            package->phase = BODY;
            if (!package->size) {
                if (fs_chmod(package->path, package->mode)) return -1;
                package->phase = HEADER; package->wanted = 10; package->used = 0;
            }
        }
    }
    package->processed += input_length;
    if (package->progress && package->total)
        package->progress(package->processed, package->total, package->progress_context);
    return 0;
}
static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static int package_finish(Package *package, const char *expected)
{
    if (!expected || strlen(expected) != 64 || package->phase != COMPLETE) return -1;
    unsigned char actual[32]; setup_sha256_final(&package->hash, actual);
    for (int i = 0; i < 32; i++) {
        int high = hex_value(expected[2*i]), low = hex_value(expected[2*i+1]);
        if (high < 0 || low < 0 || actual[i] != (high << 4 | low)) return -1;
    }
    return fs_sync();
}
int setup_install_package(const char *url, const char *expected)
{
    Package package;
    memset(&package, 0, sizeof(package));
    package.phase = MAGIC; package.wanted = 4;
    setup_sha256_init(&package.hash);
    int received = net_http_get(url, 512U * 1024U * 1024U, package_chunk, &package);
    if (received < 14) return -1;
    return package_finish(&package, expected);
}
int setup_install_package_memory(const void *data, unsigned int size, const char *expected,
                                 SetupPackageProgress progress, void *context)
{
    if (!data || size < 14 || size > 512U * 1024U * 1024U) return -1;
    Package package;
    memset(&package, 0, sizeof(package));
    package.phase = MAGIC; package.wanted = 4;
    package.total = size; package.progress = progress; package.progress_context = context;
    setup_sha256_init(&package.hash);
    if (package.progress) package.progress(0, size, context);
    fs_bulk_begin();
    const unsigned char *bytes = data;
    for (unsigned int at = 0; at < size;) {
        unsigned int take = size - at > PACKAGE_CHUNK_SIZE ? PACKAGE_CHUNK_SIZE : size - at;
        if (package_chunk(bytes + at, take, &package)) {
            fs_bulk_end();
            return -1;
        }
        at += take;
    }
    int result = package_finish(&package, expected);
    if (fs_bulk_end()) return -1;
    return result;
}
