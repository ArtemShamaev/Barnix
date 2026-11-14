/* brm: Barnix Repository Manager, with pacman's options. A repository is
 * any HTTP(S) folder with an index and the package files it names;
 * BarnixRepo builds one.
 *
 * index2 (brm 2): name|version|file|size|sha256|description|description.ru|
 *                 category|flags|installed KiB|files
 * index  (brm 1): the first seven fields, programs only.
 *
 * A package file is either one program (.elf, installed as /usr/bin/NAME)
 * or a bundle (.bpz): blocks of [raw size][packed size][raw deflate data]
 * after "BPZ1", which unpack to a BPK1 stream of files
 * ([u16 path length][u32 mode][u32 size][path][data]... [0][0][0]). Bundles
 * are unpacked while they download, so GCC's 180 MB need little memory.
 * Flags: "base" (part of Barnix: may replace system files, preinstalled
 * ones are listed in /etc/brm/base.d), "essential" (cannot be removed).
 *
 * State in /etc/brm (readable by everyone): repo (the URL), sync (the
 * index), local (installed packages), base (what the system came with),
 * base.d/LIST (what the system came with: the image, Setup components),
 * files/NAME (the files a bundle installed). */
#include "command.h"
#include <barnix_net.h>
#include "../BDK/runtime/bnet.h"

#define DEFAULT_REPO "https://raw.githubusercontent.com/ArtemShamaev/BarnixRepo/main"
#define ELF_LIMIT (64U * 1024U * 1024U)
#define MAX_PACKAGES 384
#define INDEX_LIMIT (192U * 1024U)
#define LOCAL_LIMIT 65536U
#define BLOCK_RAW (1U << 20)
#define BLOCK_PACKED (BLOCK_RAW + BLOCK_RAW / 8 + 4096)
#define FILES_LIMIT (512U * 1024U)
enum { BASE = 1, ESSENTIAL = 2 };

typedef struct {
    char name[24], version[32], file[96], sha[65], description[100], description_ru[160], category[16];
    unsigned int size, installed, flags, files;
} Package;
static Package repo[MAX_PACKAGES], local[MAX_PACKAGES];
static int repo_count, local_count, repo_loaded;
static int noconfirm, needed, russian;
static char repo_url[256];
static unsigned char *memory;          /* everything brm got from the kernel */
static unsigned int memory_size;
static unsigned char *download;        /* whole downloads (index, programs) */
static unsigned int download_used, download_capacity, download_total, marks;

/* ------------------------------------------------------------- text */
static const char *T(const char *english) { return barnix->translate(english); }
static int same(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static unsigned int len(const char *s) { unsigned int n = 0; while (s[n]) n++; return n; }
static void copy(char *out, const char *in, unsigned int capacity)
{
    unsigned int n = 0;
    while (in[n] && n + 1 < capacity) { out[n] = in[n]; n++; }
    out[n] = 0;
}
static void add(char *out, unsigned int capacity, const char *text) { unsigned int n = len(out); if (n < capacity) copy(out + n, text, capacity - n); }
static void add_number(char *out, unsigned int capacity, unsigned int value)
{
    char digits[12], text[12]; int n = 0, k = 0;
    do digits[n++] = (char)('0' + value % 10); while (value /= 10);
    while (n) text[k++] = digits[--n];
    text[k] = 0;
    add(out, capacity, text);
}
static void print(int color, const char *a) { barnix->print(color, a); }
static void line(int color, const char *a) { barnix->println(color, a); }
static void number(int color, unsigned int value) { char text[12] = ""; add_number(text, sizeof(text), value); print(color, text); }
static void size_text(char *out, unsigned int capacity, unsigned int kib)
{
    out[0] = 0;
    if (kib >= 10240) { add_number(out, capacity, kib / 1024); add(out, capacity, " MiB"); }
    else { add_number(out, capacity, kib); add(out, capacity, " KiB"); }
}
static int error(const char *what, const char *detail)
{
    print(12, T("error: ")); print(15, T(what));
    if (detail) print(15, detail);
    barnix->puts("");
    return 1;
}
static void warning(const char *what, const char *detail)
{
    print(14, T("warning: ")); print(15, detail ? detail : ""); line(15, T(what));
}
static void heading(const char *text) { print(9, ":: "); line(15, T(text)); }
static const char *describe(const Package *p) { return russian && p->description_ru[0] ? p->description_ru : p->description; }
static int contains_ci(const char *text, const char *needle)
{
    if (!*needle) return 1;
    for (; *text; text++) {
        int i = 0;
        while (needle[i] && text[i]) {
            char a = text[i], b = needle[i];
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
            if (a != b) break;
            i++;
        }
        if (!needle[i]) return 1;
    }
    return 0;
}
static int valid_name(const char *s)
{
    unsigned int n = 0;
    for (; s[n]; n++) {
        char c = s[n];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '+')) return 0;
    }
    return n > 0 && n <= 22;
}
static int ends_with(const char *s, const char *suffix)
{
    unsigned int a = len(s), b = len(suffix);
    return a >= b && same(s + a - b, suffix);
}
static int is_bundle(const Package *p) { return ends_with(p->file, ".bpz"); }

/* ------------------------------------------------------------- the database */
static int is_root(void) { return same(barnix->current_user(), "root"); }
static int need_root(void)
{
    if (is_root()) return 0;
    error("you cannot perform this operation unless you are root.", 0);
    line(15, T("Run it with sudo, for example: sudo brm -S gsnake"));
    return 1;
}
static int read_text(const char *path, char *out, unsigned int capacity)
{
    int n = barnix->size(path);
    if (n < 0) return -1;
    if ((unsigned int)n >= capacity) n = (int)capacity - 1;
    if (n && barnix->read(path, 0, out, (unsigned int)n) != n) return -1;
    out[n] = 0;
    return n;
}
static unsigned int to_number(const char *d)
{
    unsigned int v = 0;
    for (; *d >= '0' && *d <= '9'; d++) v = v * 10 + (unsigned int)(*d - '0');
    return v;
}
static unsigned int parse_flags(const char *text)
{
    unsigned int flags = 0;
    if (contains_ci(text, "base")) flags |= BASE;
    if (contains_ci(text, "essential")) flags |= ESSENTIAL;
    return flags;
}
/* Splits one line into a package; 0 when it is one. Repository lines
 * have 7 or 10 fields, local ones name|version|description|description.ru
 * [|category|flags]. */
static int parse(char *text, Package *p, int repository)
{
    char *part[11];
    int count = 0;
    part[count++] = text;
    for (char *c = text; *c && count < 11; c++) if (*c == '|') { *c = 0; part[count++] = c + 1; }
    for (unsigned int i = 0; i < sizeof(*p); i++) ((unsigned char *)p)[i] = 0;
    if (count < (repository ? 7 : 2)) return -1;
    copy(p->name, part[0], sizeof(p->name));
    copy(p->version, part[1], sizeof(p->version));
    if (!valid_name(p->name) || !p->version[0]) return -1;
    if (repository) {
        copy(p->file, part[2], sizeof(p->file));
        p->size = to_number(part[3]);
        copy(p->sha, part[4], sizeof(p->sha));
        copy(p->description, part[5], sizeof(p->description));
        copy(p->description_ru, part[6], sizeof(p->description_ru));
        if (count > 7) copy(p->category, part[7], sizeof(p->category));
        if (count > 8) p->flags = parse_flags(part[8]);
        p->installed = count > 9 ? to_number(part[9]) : (p->size + 1023) / 1024;
        p->files = count > 10 ? to_number(part[10]) : 1;
        if (len(p->sha) != 64 || !p->size) return -1;
        if (!is_bundle(p) && p->size > ELF_LIMIT) return -1;
    } else {
        if (count > 2) copy(p->description, part[2], sizeof(p->description));
        if (count > 3) copy(p->description_ru, part[3], sizeof(p->description_ru));
        if (count > 4) copy(p->category, part[4], sizeof(p->category));
        if (count > 5) p->flags = parse_flags(part[5]);
    }
    if (!p->category[0]) copy(p->category, "apps", sizeof(p->category));
    return 0;
}
static int load_lines(char *text, Package *list, int capacity, int repository)
{
    int count = 0;
    for (char *start = text; *start && count < capacity;) {
        char *end = start;
        while (*end && *end != '\n') end++;
        char saved = *end; *end = 0;
        if (end > start && end[-1] == '\r') end[-1] = 0;
        if (*start && *start != '#' && !parse(start, &list[count], repository)) count++;
        *end = saved;
        start = *end ? end + 1 : end;
    }
    return count;
}
static void load_repo_url(void)
{
    char text[256];
    if (read_text("/etc/brm/repo", text, sizeof(text)) > 0) {
        unsigned int n = 0;
        while (text[n] && text[n] != '\n' && text[n] != '\r' && text[n] != ' ') n++;
        text[n] = 0;
        if (n) { copy(repo_url, text, sizeof(repo_url)); return; }
    }
    copy(repo_url, DEFAULT_REPO, sizeof(repo_url));
}
static int load_repo(void)
{
    static char text[INDEX_LIMIT];
    if (read_text("/etc/brm/sync", text, sizeof(text)) < 0) return -1;
    repo_count = load_lines(text, repo, MAX_PACKAGES, 1);
    repo_loaded = 1;
    return 0;
}
static Package *find(Package *list, int count, const char *name)
{
    for (int i = 0; i < count; i++) if (same(list[i].name, name)) return &list[i];
    return 0;
}
/* Installed: /etc/brm/local, then what the system came with (/etc/brm/base.d). */
static int base_from;   /* local[base_from..] came from /etc/brm/base.d, not installed by brm */
static void load_local(void)
{
    static char text[LOCAL_LIMIT];
    local_count = read_text("/etc/brm/local", text, sizeof(text)) < 0 ? 0 : load_lines(text, local, MAX_PACKAGES, 0);
    base_from = local_count;
    /* /etc/brm/base.d/LIST: one file per part of the system (the image,
     * each Setup component). */
    static Package base[MAX_PACKAGES];
    unsigned int position = 0;
    BarnixDirEntry entry;
    while (barnix->directory("/etc/brm/base.d", &position, &entry) > 0) {
        if (entry.kind != 8) continue;
        char path[300] = "/etc/brm/base.d/";
        add(path, sizeof(path), entry.name);
        int base_count = read_text(path, text, sizeof(text)) < 0 ? 0 : load_lines(text, base, MAX_PACKAGES, 0);
        for (int i = 0; i < base_count && local_count < MAX_PACKAGES; i++)
            if (!find(local, local_count, base[i].name)) { base[i].flags |= BASE; local[local_count++] = base[i]; }
    }
}
static void add_field(char *text, unsigned int capacity, const char *value, const char *separator)
{ add(text, capacity, value); add(text, capacity, separator); }
static int save_local(void)
{
    static char text[LOCAL_LIMIT];
    text[0] = 0;
    add(text, sizeof(text), "# brm: installed packages (name|version|description|description.ru|category|flags)\n");
    for (int i = 0; i < base_from; i++) {
        Package *p = &local[i];
        add_field(text, sizeof(text), p->name, "|"); add_field(text, sizeof(text), p->version, "|");
        add_field(text, sizeof(text), p->description, "|"); add_field(text, sizeof(text), p->description_ru, "|");
        add_field(text, sizeof(text), p->category, "|");
        add(text, sizeof(text), p->flags & ESSENTIAL ? "base,essential" : p->flags & BASE ? "base" : "");
        add(text, sizeof(text), "\n");
    }
    barnix->mkdir("/etc/brm");
    return barnix->write("/etc/brm/local", text, (int)len(text));
}
/* Records p as installed by brm (it leaves the base list's shadow). */
static int remember(const Package *p)
{
    Package *old = find(local, base_from, p->name);
    if (!old) {
        Package *shadow = find(local + base_from, local_count - base_from, p->name);
        if (shadow) *shadow = local[base_from];   /* the first base entry moves into its place */
        else if (local_count < MAX_PACKAGES) local[local_count++] = local[base_from];
        else return error("too many installed packages", 0);
        old = &local[base_from++];
    }
    unsigned int flags = old->flags;
    *old = *p;
    old->flags |= flags & BASE;
    return save_local();
}
/* A removed package leaves the lists of what the system came with too. */
static void base_forget(const char *name)
{
    static char text[LOCAL_LIMIT], kept[LOCAL_LIMIT];
    unsigned int position = 0, n = len(name);
    BarnixDirEntry entry;
    while (barnix->directory("/etc/brm/base.d", &position, &entry) > 0) {
        if (entry.kind != 8) continue;
        char path[300] = "/etc/brm/base.d/";
        add(path, sizeof(path), entry.name);
        if (read_text(path, text, sizeof(text)) < 0) continue;
        unsigned int used = 0; int changed = 0;
        for (char *line = text; *line;) {
            char *end = line; while (*end && *end != '\n') end++;
            unsigned int length = (unsigned int)(end - line) + (*end ? 1 : 0);
            unsigned int k = 0; while (k < n && line[k] == name[k]) k++;
            if (k == n && line[n] == '|') changed = 1;
            else { for (unsigned int i = 0; i < length && used + 1 < sizeof(kept); i++) kept[used++] = line[i]; }
            line += length;
        }
        if (changed) barnix->write(path, kept, (int)used);
    }
}
static int forget(const char *name)
{
    base_forget(name);
    Package *p = find(local, base_from, name);
    if (p) { *p = local[--base_from]; local[base_from] = local[--local_count]; }
    else {
        p = find(local + base_from, local_count - base_from, name);
        if (p) *p = local[--local_count];
    }
    return save_local();
}

/* ------------------------------------------------------------- downloads */
static void progress(unsigned int done)
{
    if (!download_total) return;
    unsigned int want = download_total >= 0x08000000U ? done / (download_total / 20) : done * 20 / download_total;
    while (marks < want && marks < 20) { print(10, "#"); marks++; }
}
static int store(void *context, const void *data, unsigned int length)
{
    (void)context;
    if (download_used + length > download_capacity) return 1;
    const unsigned char *p = data;
    for (unsigned int i = 0; i < length; i++) download[download_used + i] = p[i];
    download_used += length;
    progress(download_used);
    return 0;
}
static int request(const char *url, int (*sink)(void *, const void *, unsigned int), char *why, unsigned int why_size, int *status)
{
    static BnetRequest r;
    for (unsigned int i = 0; i < sizeof(r); i++) ((unsigned char *)&r)[i] = 0;
    r.data = sink;
    int result = bnet_request(url, &r);
    if (status) *status = r.status;
    if (result) { copy(why, r.error, why_size); return -1; }
    return 0;
}
static int fetch(const char *url, unsigned int expected, char *why, unsigned int why_size, int *status)
{
    download_used = 0; download_total = expected; marks = 0;
    if (request(url, store, why, why_size, status)) {
        if (download_used >= download_capacity) copy(why, T("the file is too large"), why_size);
        return -1;
    }
    return 0;
}
static void join(char *out, unsigned int capacity, const char *file)
{
    copy(out, repo_url, capacity);
    unsigned int n = len(out);
    if (n && out[n - 1] != '/') add(out, capacity, "/");
    add(out, capacity, file);
}
static int sync_databases(void)
{
    heading("Synchronizing package databases...");
    char url[400], why[160];
    print(15, " barnix  ");
    int status = 0, v2 = 1;
    join(url, sizeof(url), "index2");
    if (fetch(url, 0, why, sizeof(why), &status)) {
        if (status != 404) { barnix->puts(""); return error("failed to retrieve the repository index: ", why); }
        v2 = 0;   /* a repository for brm 1 */
        join(url, sizeof(url), "index");
        if (fetch(url, 0, why, sizeof(why), 0)) { barnix->puts(""); return error("failed to retrieve the repository index: ", why); }
    }
    const char *magic = "# Barnix repository";
    for (unsigned int i = 0; magic[i]; i++)
        if (download_used <= i || download[i] != (unsigned char)magic[i]) {
            barnix->puts("");
            return error("this address is not a Barnix repository: ", url);
        }
    if (download_used >= INDEX_LIMIT) { barnix->puts(""); return error("the repository index is too large", 0); }
    number(15, (download_used + 1023) / 1024); print(15, " KiB"); line(8, v2 ? "" : " (v1)");
    barnix->mkdir("/etc/brm");
    if (barnix->write("/etc/brm/sync", (const char *)download, (int)download_used)) return error("cannot write /etc/brm/sync", 0);
    return load_repo();
}
static int ensure_repo(void)
{
    if (repo_loaded) return 0;
    if (!load_repo()) return 0;
    if (!is_root()) return error("the package list is not downloaded yet; run: sudo brm -Sy", 0);
    /* The first use: fetch the index without being asked. */
    return sync_databases();
}

/* ------------------------------------------------------------- versions */
/* Compares dotted versions numerically: 1.10 > 1.9. */
static int version_compare(const char *a, const char *b)
{
    while (*a || *b) {
        unsigned int x = 0, y = 0;
        while (*a >= '0' && *a <= '9') x = x * 10 + (unsigned int)(*a++ - '0');
        while (*b >= '0' && *b <= '9') y = y * 10 + (unsigned int)(*b++ - '0');
        if (x != y) return x < y ? -1 : 1;
        if (*a && (*a < '0' || *a > '9')) a++;
        if (*b && (*b < '0' || *b > '9')) b++;
    }
    return 0;
}

/* ------------------------------------------------------------- installing */
static int confirm(const char *question)
{
    print(9, ":: "); print(15, T(question)); print(15, " [Y/n] ");
    if (noconfirm) { barnix->puts(""); return 1; }
    for (;;) {
        unsigned int key = barnix->getch();
        if (key == '\n' || key == '\r' || key == 'y' || key == 'Y' || key == 0x43d /* н */ || key == 0x41d) { barnix->puts("Y"); return 1; }
        if (key == 'n' || key == 'N' || key == 0x442 /* т */ || key == 0x422 || key == 27) { barnix->puts("n"); return 0; }
    }
}
static void hex(const unsigned char *digest, char out[65])
{
    for (int i = 0; i < 32; i++) { out[2 * i] = "0123456789abcdef"[digest[i] >> 4]; out[2 * i + 1] = "0123456789abcdef"[digest[i] & 15]; }
    out[64] = 0;
}
static int check_elf(const unsigned char *data, unsigned int size, const char *name)
{
    if (size < 52 || data[0] != 0x7f || data[1] != 'E' || data[2] != 'L' || data[3] != 'F' || data[4] != 1)
        return error("not a Barnix program: ", name);
    if (data[7] == 255 && data[8] > barnix->version) {
        char text[64] = "";
        add(text, sizeof(text), name); add(text, sizeof(text), " (ABI "); add_number(text, sizeof(text), data[8]); add(text, sizeof(text), ")");
        return error("the package needs a newer Barnix: ", text);
    }
    return 0;
}
static int exists(const char *path) { return barnix->size(path) >= 0; }
static int make_executable(const char *path)
{
    /* ABI 14 sets the mode; older kernels: an x right also sets the bit. */
    if (barnix->version >= 14) return barnix->file_mode(path, 0755);
    return barnix->chmod(path, "root=rx", "");
}
static void files_path(char *out, unsigned int capacity, const char *name)
{ copy(out, "/etc/brm/files/", capacity); add(out, capacity, name); }
/* The files an installed package owns, one per line (programs: /usr/bin/NAME). */
static int owned_files(const Package *p, char *out, unsigned int capacity)
{
    char path[64]; files_path(path, sizeof(path), p->name);
    int n = read_text(path, out, capacity);
    if (n >= 0) return n;
    copy(out, "/usr/bin/", capacity); add(out, capacity, p->name); add(out, capacity, "\n");
    return (int)len(out);
}
static int listed(const char *list, const char *path)
{
    unsigned int n = len(path);
    for (const char *p = list; *p;) {
        unsigned int i = 0;
        while (i < n && p[i] == path[i]) i++;
        if (i == n && (p[i] == '\n' || !p[i])) return 1;
        while (*p && *p != '\n') p++;
        if (*p) p++;
    }
    return 0;
}

/* A program: one ELF file into /usr/bin. */
static int place(const Package *p, const unsigned char *data, unsigned int size)
{
    char path[64] = "/usr/bin/";
    add(path, sizeof(path), p->name);
    if (barnix->write(path, (const char *)data, (int)size)) return error("cannot write ", path);
    if (make_executable(path)) return error("cannot make it executable: ", path);
    char legacy[64] = "/bin/";
    add(legacy, sizeof(legacy), p->name);
    if (exists(legacy)) barnix->rm(legacy);   /* packages before the move to /usr/bin */
    char list[64]; files_path(list, sizeof(list), p->name);
    if (exists(list)) barnix->rm(list);
    return remember(p);
}

/* A bundle, unpacked while it downloads. */
static struct {
    const Package *package;
    const char *old_files;          /* the previous version's files (may be replaced) */
    unsigned char *packed, *raw;    /* one block each */
    unsigned int packed_used, raw_size, packed_size, stage, bpz_seen, bpk_seen;
    unsigned char header[10]; unsigned int header_used;
    char path[260]; unsigned int path_length, path_used, mode, remaining, written;
    char *list; unsigned int list_used;   /* the files written so far */
    unsigned int files, received;
    char last_dir[260];
    Sha256 hash;
    char why[200];
} bz;
static int bundle_fail(const char *what, const char *detail)
{
    copy(bz.why, T(what), sizeof(bz.why));
    if (detail) add(bz.why, sizeof(bz.why), detail);
    return 1;
}
static int make_parents(const char *path)
{
    char dir[260]; copy(dir, path, sizeof(dir));
    int last = -1;
    for (int i = 0; dir[i]; i++) if (dir[i] == '/') last = i;
    if (last <= 0) return 0;
    dir[last] = 0;
    if (same(dir, bz.last_dir)) return 0;
    for (int i = 1; dir[i - 1]; i++)
        if (dir[i] == '/' || !dir[i]) {
            char saved = dir[i]; dir[i] = 0;
            unsigned int position = 0; BarnixDirEntry entry;
            if (barnix->directory(dir, &position, &entry) < 0 && barnix->mkdir(dir)) { dir[i] = saved; return -1; }
            dir[i] = saved;
            if (!saved) break;
        }
    copy(bz.last_dir, dir, sizeof(bz.last_dir));
    return 0;
}
static int entry_begin(void)
{
    const char *path = bz.path;
    if (path[0] != '/' || contains_ci(path, "/../") || contains_ci(path, "/./") || ends_with(path, "/..") ||
        (bz.mode != 0100644 && bz.mode != 0100755)) return bundle_fail("unsafe file in the package: ", path);
    if (exists(path) && !(bz.package->flags & BASE) && !listed(bz.old_files, path))
        return bundle_fail("file conflict, it exists in the system: ", path);
    if (make_parents(path)) return bundle_fail("cannot make the folder for ", path);
    if (barnix->write(path, "", 0)) return bundle_fail("cannot write ", path);
    unsigned int n = len(path);
    if (bz.list_used + n + 2 >= FILES_LIMIT) return bundle_fail("the package has too many files", 0);
    copy(bz.list + bz.list_used, path, n + 1);
    bz.list_used += n; bz.list[bz.list_used++] = '\n'; bz.list[bz.list_used] = 0;
    bz.files++;
    bz.written = 0;
    return 0;
}
static int entry_end(void)
{
    if ((bz.mode & 0111) && make_executable(bz.path)) return bundle_fail("cannot make it executable: ", bz.path);
    return 0;
}
/* The unpacked BPK1 stream. */
static int bpk_feed(const unsigned char *data, unsigned int length)
{
    while (length) {
        if (bz.stage == 4) return bundle_fail("data after the end of the package", 0);
        if (bz.bpk_seen < 4) {   /* "BPK1" */
            if (*data != (unsigned char)"BPK1"[bz.bpk_seen]) return bundle_fail("this is not a Barnix package", 0);
            data++; length--; bz.bpk_seen++;
            continue;
        }
        if (bz.stage == 0) {   /* entry header */
            bz.header[bz.header_used++] = *data++; length--;
            if (bz.header_used < 10) continue;
            bz.header_used = 0;
            bz.path_length = bz.header[0] | (unsigned int)bz.header[1] << 8;
            bz.mode = bz.header[2] | (unsigned int)bz.header[3] << 8 | (unsigned int)bz.header[4] << 16 | (unsigned int)bz.header[5] << 24;
            bz.remaining = bz.header[6] | (unsigned int)bz.header[7] << 8 | (unsigned int)bz.header[8] << 16 | (unsigned int)bz.header[9] << 24;
            if (!bz.path_length) { bz.stage = 4; continue; }
            if (bz.path_length >= sizeof(bz.path)) return bundle_fail("a file name in the package is too long", 0);
            bz.path_used = 0; bz.stage = 1;
        } else if (bz.stage == 1) {   /* path */
            bz.path[bz.path_used++] = (char)*data++; length--;
            if (bz.path_used < bz.path_length) continue;
            bz.path[bz.path_used] = 0;
            if (entry_begin()) return 1;
            bz.stage = 2;
            if (!bz.remaining) { if (entry_end()) return 1; bz.stage = 0; }
        } else {   /* data */
            unsigned int take = length < bz.remaining ? length : bz.remaining;
            if (barnix->append(bz.path, (const char *)data, (int)take)) return bundle_fail("cannot write ", bz.path);
            data += take; length -= take; bz.remaining -= take;
            if (!bz.remaining) { if (entry_end()) return 1; bz.stage = 0; }
        }
    }
    return 0;
}
/* The downloaded BPZ1 stream. */
static int bundle_data(void *context, const void *data, unsigned int length)
{
    (void)context;
    const unsigned char *in = data;
    sha256_update(&bz.hash, in, length);
    bz.received += length;
    progress(bz.received);
    while (length) {
        unsigned int want = bz.packed_size ? 8 + bz.packed_size : 8;
        unsigned int take = want - bz.packed_used;
        if (take > length) take = length;
        for (unsigned int i = 0; i < take; i++) bz.packed[bz.packed_used + i] = in[i];
        bz.packed_used += take; in += take; length -= take;
        if (bz.packed_used < want) break;
        if (!bz.packed_size) {
            bz.raw_size = bz.packed[0] | (unsigned int)bz.packed[1] << 8 | (unsigned int)bz.packed[2] << 16 | (unsigned int)bz.packed[3] << 24;
            bz.packed_size = bz.packed[4] | (unsigned int)bz.packed[5] << 8 | (unsigned int)bz.packed[6] << 16 | (unsigned int)bz.packed[7] << 24;
            if (!bz.raw_size && !bz.packed_size) { bz.packed_used = 0; continue; }   /* the end */
            if (bz.raw_size > BLOCK_RAW || !bz.packed_size || bz.packed_size + 8 > BLOCK_PACKED)
                return bundle_fail("the package is damaged", 0);
            continue;
        }
        unsigned long used = 0;
        long n = bnet_inflate(bz.packed + 8, bz.packed_size, &used, bz.raw, BLOCK_RAW, 0);
        if (n != (long)bz.raw_size) return bundle_fail("the package is damaged", 0);
        if (bpk_feed(bz.raw, (unsigned int)n)) return 1;
        bz.packed_used = 0; bz.packed_size = 0; bz.raw_size = 0;
    }
    return 0;
}
/* The download: "BPZ1", then the blocks. */
static int bundle_sink(void *context, const void *data, unsigned int length)
{
    const unsigned char *in = data;
    while (length && bz.bpz_seen < 4) {
        if (*in != (unsigned char)"BPZ1"[bz.bpz_seen]) return bundle_fail("this is not a Barnix package", 0);
        sha256_update(&bz.hash, in, 1);
        bz.received++; bz.bpz_seen++;
        in++; length--;
    }
    return length ? bundle_data(context, in, length) : 0;
}
static void remove_listed(const char *list, const char *keep)
{
    char path[260];
    for (const char *p = list; *p;) {
        unsigned int n = 0;
        while (p[n] && p[n] != '\n' && n + 1 < sizeof(path)) { path[n] = p[n]; n++; }
        path[n] = 0;
        if (path[0] == '/' && (!keep || !listed(keep, path)) && exists(path)) barnix->rm(path);
        while (*p && *p != '\n') p++;
        if (*p) p++;
    }
}
static int install_bundle(const Package *p, const char *url)
{
    for (unsigned int i = 0; i < sizeof(bz); i++) ((unsigned char *)&bz)[i] = 0;
    bz.package = p;
    /* memory: [old files][new files][packed block][raw block] */
    if (memory_size < 2 * FILES_LIMIT + BLOCK_PACKED + BLOCK_RAW) return error("not enough memory for this package", 0);
    char *old = (char *)memory;
    bz.list = (char *)memory + FILES_LIMIT;
    bz.packed = memory + 2 * FILES_LIMIT;
    bz.raw = bz.packed + BLOCK_PACKED;
    bz.list[0] = 0;
    old[0] = 0;
    if (find(local, local_count, p->name)) owned_files(p, old, FILES_LIMIT);
    bz.old_files = old;
    sha256_init(&bz.hash);
    download_total = p->size; marks = 0;
    char why[160] = "";
    int failed = request(url, bundle_sink, why, sizeof(why), 0);
    while (marks < 20) { print(10, "#"); marks++; }
    print(15, "] ");
    char text[24]; size_text(text, sizeof(text), (bz.received + 1023) / 1024); line(15, text);
    unsigned char digest[32]; char sha[65];
    sha256_final(&bz.hash, digest); hex(digest, sha);
    if (!failed && (bz.stage != 4 || bz.packed_used)) { failed = 1; copy(bz.why, T("the package ended too early"), sizeof(bz.why)); }
    if (!failed && (bz.received != p->size || !same(sha, p->sha))) { failed = 1; copy(bz.why, T("invalid or corrupted package (checksum): "), sizeof(bz.why)); add(bz.why, sizeof(bz.why), p->name); }
    if (failed) {
        /* new files go again; files of an older version that were rewritten stay */
        remove_listed(bz.list, old);
        return error(bz.why[0] ? bz.why : "failed retrieving file: ", bz.why[0] ? 0 : why);
    }
    remove_listed(old, bz.list);   /* what the new version no longer has */
    char path[64];
    barnix->mkdir("/etc/brm"); barnix->mkdir("/etc/brm/files");
    files_path(path, sizeof(path), p->name);
    if (barnix->write(path, bz.list, (int)bz.list_used)) return error("cannot write ", path);
    print(15, "    "); number(15, bz.files); line(15, T(" files"));
    return remember(p);
}
static int install(const char *const *names, int count, int upgrade_only)
{
    if (need_root() || ensure_repo()) return 1;
    load_local();
    Package *targets[MAX_PACKAGES];
    int n = 0;
    unsigned int total = 0, installed = 0;
    for (int i = 0; i < count; i++) {
        Package *p = find(repo, repo_count, names[i]);
        if (!p) return error("target not found: ", names[i]);
        int duplicate = 0;
        for (int k = 0; k < n; k++) if (targets[k] == p) duplicate = 1;
        if (duplicate) continue;
        Package *have = find(local, local_count, p->name);
        char path[64] = "/usr/bin/";
        add(path, sizeof(path), p->name);
        if (!have && !is_bundle(p) && !(p->flags & BASE) && exists(path)) {
            error("file conflict, it exists in the system: ", path);
            return 1;
        }
        if (have && !version_compare(have->version, p->version)) {
            char what[64] = "";
            add(what, sizeof(what), p->name); add(what, sizeof(what), "-"); add(what, sizeof(what), p->version);
            if (needed || upgrade_only) { warning(" is up to date -- skipping", what); continue; }
            warning(" is up to date -- reinstalling", what);
        }
        targets[n++] = p;
        total += (p->size + 1023) / 1024;
        installed += p->installed;
    }
    if (!n) { if (!upgrade_only) line(15, T(" there is nothing to do")); return 0; }
    /* Room on the disk, before anything is downloaded. New packages need
     * all of theirs; an update rewrites its files one by one. */
    unsigned int need_kib = 0, need_files = 0, free_kib = 0, free_files = 0;
    for (int i = 0; i < n; i++)
        if (!find(local, local_count, targets[i]->name)) { need_kib += targets[i]->installed; need_files += targets[i]->files; }
    if (need_kib && barnix->version >= 14 && barnix->disk_space && !barnix->disk_space("/usr", &free_kib, &free_files) &&
        (free_kib < need_kib + need_kib / 32 + 256 || free_files < need_files + 16)) {
        char text[32];
        error(free_kib < need_kib + need_kib / 32 + 256 ? "not enough free space on the disk" : "not enough free file slots (inodes) on the disk", 0);
        size_text(text, sizeof(text), need_kib); print(15, T("    needed: ")); print(15, text);
        print(15, ", "); number(15, need_files); line(15, T(" files"));
        size_text(text, sizeof(text), free_kib); print(15, T("    free:   ")); print(15, text);
        print(15, ", "); number(15, free_files); line(15, T(" files"));
        line(14, T("The Live CD keeps its files in a small RAM disk: install Barnix to a disk (Setup) for big packages."));
        return 1;
    }
    barnix->puts("");
    print(15, T("Packages")); print(15, " ("); number(15, (unsigned int)n); print(15, ")  ");
    for (int i = 0; i < n; i++) { print(11, targets[i]->name); print(15, "-"); print(15, targets[i]->version); print(15, "  "); }
    barnix->puts("");
    barnix->puts("");
    char text[24];
    size_text(text, sizeof(text), total); print(15, T("Total Download Size:   ")); line(15, text);
    size_text(text, sizeof(text), installed); print(15, T("Total Installed Size:  ")); line(15, text);
    barnix->puts("");
    if (!confirm("Proceed with installation?")) return 1;
    heading("Retrieving packages...");
    int failures = 0;
    for (int i = 0; i < n; i++) {
        Package *p = targets[i];
        char url[400], why[160], label[48] = " ";
        join(url, sizeof(url), p->file);
        add(label, sizeof(label), p->name); add(label, sizeof(label), "-"); add(label, sizeof(label), p->version);
        while (len(label) < 26) add(label, sizeof(label), " ");
        print(15, label); print(15, "[");
        if (is_bundle(p)) {
            if (install_bundle(p, url)) failures++;
            continue;
        }
        if (fetch(url, p->size, why, sizeof(why), 0)) { line(15, "]"); failures += error("failed retrieving file: ", why); continue; }
        while (marks < 20) { print(10, "#"); marks++; }
        print(15, "] "); size_text(text, sizeof(text), (download_used + 1023) / 1024); line(15, text);
        /* checking package integrity */
        Sha256 hash;
        unsigned char digest[32];
        char sha[65];
        sha256_init(&hash); sha256_update(&hash, download, download_used); sha256_final(&hash, digest);
        hex(digest, sha);
        if (download_used != p->size || !same(sha, p->sha)) { failures += error("invalid or corrupted package (checksum): ", p->name); continue; }
        if (check_elf(download, download_used, p->name)) { failures++; continue; }
        print(15, "("); number(15, (unsigned int)i + 1); print(15, "/"); number(15, (unsigned int)n); print(15, ") ");
        print(15, T("installing ")); line(11, p->name);
        if (place(p, download, download_used)) failures++;
    }
    barnix->sync();
    if (failures) return 1;
    print(10, T("Done. ")); line(15, T("Run a program by its name, or in Happy Gouse: Run program."));
    return 0;
}
static int upgrade(void)
{
    if (need_root() || ensure_repo()) return 1;
    load_local();
    heading("Starting full system upgrade...");
    const char *names[MAX_PACKAGES];
    int n = 0;
    for (int i = 0; i < local_count; i++) {
        Package *p = find(repo, repo_count, local[i].name);
        if (p && version_compare(local[i].version, p->version) < 0) names[n++] = local[i].name;
    }
    if (!n) { line(15, T(" there is nothing to do")); return 0; }
    return install(names, n, 1);
}
static int remove_packages(const char *const *names, int count)
{
    if (need_root()) return 1;
    load_local();
    for (int i = 0; i < count; i++) {
        Package *p = find(local, local_count, names[i]);
        if (!p) return error("target not found: ", names[i]);
        if (p->flags & ESSENTIAL) return error("this package is essential for Barnix and cannot be removed: ", names[i]);
        if (same(p->name, "brm")) return error("brm cannot remove itself", 0);
    }
    barnix->puts("");
    print(15, T("Packages")); print(15, " ("); number(15, (unsigned int)count); print(15, ")  ");
    for (int i = 0; i < count; i++) {
        Package *p = find(local, local_count, names[i]);
        print(11, p->name); print(15, "-"); print(15, p->version); print(15, "  ");
    }
    barnix->puts("");
    barnix->puts("");
    if (!confirm("Do you want to remove these packages?")) return 1;
    for (int i = 0; i < count; i++) {
        Package *p = find(local, local_count, names[i]);
        if (!p) continue;
        print(15, "("); number(15, (unsigned int)i + 1); print(15, "/"); number(15, (unsigned int)count); print(15, ") ");
        print(15, T("removing ")); line(11, p->name);
        if (memory_size < FILES_LIMIT) return error("not enough memory", 0);
        char *files = (char *)memory;
        owned_files(p, files, FILES_LIMIT);
        remove_listed(files, 0);
        /* Packages installed before the move to /usr/bin live in /bin. */
        char legacy[64] = "/bin/";
        add(legacy, sizeof(legacy), p->name);
        if (exists(legacy)) barnix->rm(legacy);
        char list[64]; files_path(list, sizeof(list), p->name);
        if (exists(list)) barnix->rm(list);
        if (forget(names[i])) return error("cannot write /etc/brm/local", 0);
    }
    barnix->sync();
    return 0;
}
/* -U FILE: installs a program file from the disk as a package. */
static int install_file(const char *file)
{
    if (need_root()) return 1;
    const char *base = file;
    for (const char *c = file; *c; c++) if (*c == '/') base = c + 1;
    Package p;
    for (unsigned int i = 0; i < sizeof(p); i++) ((unsigned char *)&p)[i] = 0;
    unsigned int n = 0;
    while (base[n] && base[n] != '.' && n + 1 < sizeof(p.name)) { p.name[n] = base[n]; n++; }
    if (!valid_name(p.name)) return error("the file name must be NAME.elf with a-z, 0-9, _ or -: ", base);
    copy(p.version, "local", sizeof(p.version));
    copy(p.description, "Installed from a file", sizeof(p.description));
    copy(p.description_ru, "Установлен из файла", sizeof(p.description_ru));
    copy(p.category, "apps", sizeof(p.category));
    int size = barnix->size(file);
    if (size <= 0 || (unsigned int)size > download_capacity) return error("cannot read ", file);
    if (barnix->read(file, 0, download, (unsigned int)size) != size) return error("cannot read ", file);
    if (check_elf(download, (unsigned int)size, p.name)) return 1;
    load_local();
    char path[64] = "/usr/bin/";
    add(path, sizeof(path), p.name);
    if (!find(local, local_count, p.name) && exists(path)) return error("file conflict, it exists in the system: ", path);
    print(15, T("installing ")); line(11, p.name);
    return place(&p, download, (unsigned int)size);
}

/* ------------------------------------------------------------- queries */
static void show_package(const Package *p, const Package *installed, int brief)
{
    print(13, p->category[0] ? p->category : "barnix"); print(13, "/"); print(15, p->name); print(15, " "); print(10, p->version);
    if (installed) {
        print(11, T(" [installed"));
        if (version_compare(installed->version, p->version)) { print(11, ": "); print(11, installed->version); }
        print(11, "]");
    }
    barnix->puts("");
    if (!brief) { print(15, "    "); line(15, describe(p)); }
}
static int search(const char *query, int installed_only)
{
    load_local();
    if (installed_only) {
        for (int i = 0; i < local_count; i++)
            if (contains_ci(local[i].name, query) || contains_ci(describe(&local[i]), query)) {
                print(13, "local/"); print(15, local[i].name); print(15, " "); line(10, local[i].version);
                print(15, "    "); line(15, describe(&local[i]));
            }
        return 0;
    }
    if (ensure_repo()) return 1;
    int found = 0;
    for (int i = 0; i < repo_count; i++)
        if (contains_ci(repo[i].name, query) || contains_ci(repo[i].description, query) ||
            contains_ci(repo[i].description_ru, query) || same(repo[i].category, query)) {
            show_package(&repo[i], find(local, local_count, repo[i].name), 0);
            found++;
        }
    return found ? 0 : 1;
}
static void field(const char *name, const char *value)
{
    char label[40] = "";
    add(label, sizeof(label), T(name));
    /* pad by characters, not bytes */
    int columns = 0;
    for (const char *c = label; *c; c++) if (((unsigned char)*c & 0xc0) != 0x80) columns++;
    while (columns++ < 16) add(label, sizeof(label), " ");
    print(15, label); print(15, ": "); line(15, value);
}
static int info(const char *const *names, int count, int installed_only)
{
    load_local();
    if (!installed_only && ensure_repo()) return 1;
    for (int i = 0; i < count; i++) {
        Package *p = installed_only ? find(local, local_count, names[i]) : find(repo, repo_count, names[i]);
        if (!p) return error(installed_only ? "package was not found: " : "target not found: ", names[i]);
        char size[24] = "";
        if (!installed_only) field("Repository", "barnix");
        field("Name", p->name);
        field("Version", p->version);
        field("Description", describe(p));
        field("Category", p->category);
        if (p->flags & BASE) field("Part of", p->flags & ESSENTIAL ? T("Barnix (essential)") : "Barnix");
        if (!installed_only) {
            size_text(size, sizeof(size), (p->size + 1023) / 1024);
            field("Download Size", size);
            size_text(size, sizeof(size), p->installed);
            field("Installed Size", size);
            field("SHA-256", p->sha);
            Package *installed = find(local, local_count, p->name);
            field("Installed", installed ? installed->version : T("no"));
        }
        if (!installed_only && is_bundle(p)) field("Files", T("a bundle (brm -Ql NAME lists them)"));
        else if (installed_only && memory_size >= FILES_LIMIT) {
            char *files = (char *)memory;
            int n = owned_files(p, files, FILES_LIMIT), lines = 0;
            for (int k = 0; k < n; k++) if (files[k] == '\n') lines++;
            char text[24] = ""; add_number(text, sizeof(text), (unsigned int)lines);
            field("Files", text);
        } else {
            char path[64] = "/usr/bin/";
            add(path, sizeof(path), p->name);
            field("File", path);
        }
        barnix->puts("");
    }
    return 0;
}
static int list_files(const char *const *names, int count)
{
    load_local();
    if (memory_size < FILES_LIMIT) return error("not enough memory", 0);
    for (int i = 0; i < count; i++) {
        Package *p = find(local, local_count, names[i]);
        if (!p) return error("package was not found: ", names[i]);
        char *files = (char *)memory;
        owned_files(p, files, FILES_LIMIT);
        for (char *f = files; *f;) {
            char *e = f; while (*e && *e != '\n') e++;
            char saved = *e; *e = 0;
            if (*f) { print(15, p->name); print(15, " "); line(15, f); }
            *e = saved; f = *e ? e + 1 : e;
        }
    }
    return 0;
}
static int list_repo(void)
{
    if (ensure_repo()) return 1;
    load_local();
    for (int i = 0; i < repo_count; i++) {
        print(13, repo[i].category); print(15, " "); print(15, repo[i].name); print(15, " "); print(10, repo[i].version);
        if (find(local, local_count, repo[i].name)) print(11, T(" [installed]"));
        barnix->puts("");
    }
    return 0;
}
static int query(const char *const *names, int count, int upgrades)
{
    load_local();
    if (upgrades && ensure_repo()) return 1;
    int shown = 0;
    for (int i = 0; i < local_count; i++) {
        if (count) { int wanted = 0; for (int k = 0; k < count; k++) if (same(names[k], local[i].name)) wanted = 1; if (!wanted) continue; }
        Package *p = upgrades ? find(repo, repo_count, local[i].name) : 0;
        if (upgrades && (!p || version_compare(local[i].version, p->version) >= 0)) continue;
        print(15, local[i].name); print(15, " "); print(10, local[i].version);
        if (p) { print(15, " -> "); print(10, p->version); }
        barnix->puts("");
        shown++;
    }
    for (int k = 0; k < count; k++) if (!find(local, local_count, names[k])) error("package was not found: ", names[k]);
    return upgrades && !shown ? 1 : 0;
}
static int set_repo(const char *url)
{
    if (need_root()) return 1;
    if (!(url[0] == 'h' && url[1] == 't' && url[2] == 't' && url[3] == 'p')) return error("the repository must be an http:// or https:// address: ", url);
    copy(repo_url, url, sizeof(repo_url));
    unsigned int n = len(repo_url);
    while (n > 8 && repo_url[n - 1] == '/') repo_url[--n] = 0;
    barnix->mkdir("/etc/brm");
    if (barnix->write("/etc/brm/repo", repo_url, (int)len(repo_url))) return error("cannot write /etc/brm/repo", 0);
    print(15, T("Repository: ")); line(11, repo_url);
    /* The old index belongs to the old repository. */
    barnix->rm("/etc/brm/sync");
    return sync_databases();
}
static int usage(void)
{
    line(15, T("usage:  brm <operation> [...]"));
    line(15, T("operations:"));
    line(15, T("    brm -S NAME...     install packages from the repository"));
    line(15, T("    brm -Sy            download a fresh package list"));
    line(15, T("    brm -Syu           refresh and upgrade everything"));
    line(15, T("    brm -Ss [TEXT]     search the repository"));
    line(15, T("    brm -Si NAME       package information; brm -Sl lists all"));
    line(15, T("    brm -Sc            forget the downloaded package list"));
    line(15, T("    brm -R NAME...     remove packages"));
    line(15, T("    brm -Q [NAME]      installed packages; -Qi info, -Qs search, -Qu upgrades"));
    line(15, T("    brm -Ql NAME       the files of an installed package"));
    line(15, T("    brm -U FILE.elf    install a program file"));
    line(15, T("    brm set-repo URL   choose the repository; brm repo shows it"));
    line(15, T("options: --noconfirm (do not ask), --needed (skip up-to-date)"));
    line(15, T("In Happy Gouse the same is in BarnStore."));
    return 0;
}

int main(int argc, const char *const *argv)
{
    if (barnix->version < 12) return fail("brm needs a Barnix kernel with networking (ABI 12)");
    const char *probe = barnix->translate("program exited: ");
    russian = probe && probe[0] != 'p';
    load_repo_url();
    unsigned int bytes = 0;   /* as much as there is: programs up to 64 MiB, bundle buffers */
    memory = barnix->memory(&bytes);
    memory_size = memory ? bytes : 0;
    if (!memory || bytes < 2U * 1024U * 1024U) return fail("not enough memory");
    download = memory;
    download_capacity = bytes < ELF_LIMIT ? bytes : ELF_LIMIT;

    const char *operation = 0, *targets[64];
    int count = 0;
    for (int i = 1; i < argc; i++) {
        if (same(argv[i], "--noconfirm")) noconfirm = 1;
        else if (same(argv[i], "--needed")) needed = 1;
        else if (same(argv[i], "-h") || same(argv[i], "--help")) return usage();
        else if (same(argv[i], "-V") || same(argv[i], "--version")) { line(15, "brm 2.0 - Barnix Repository Manager"); return 0; }
        else if (argv[i][0] == '-' && !operation) operation = argv[i] + 1;
        else if (argv[i][0] == '-') return error("only one operation may be used at a time", 0);
        else if (count < 64) targets[count++] = argv[i];
    }
    if (!operation) {
        if (count == 2 && same(targets[0], "set-repo")) return set_repo(targets[1]);
        if (count == 1 && same(targets[0], "repo")) { print(15, T("Repository: ")); line(11, repo_url); return 0; }
        return usage();
    }
    char op = operation[0];
    int y = 0, u = 0, s = 0, i = 0, l = 0, c = 0, q = 0;
    for (const char *m = operation + 1; *m; m++) {
        if (*m == 'y') y = 1;
        else if (*m == 'u') u = 1;
        else if (*m == 's') s = 1;
        else if (*m == 'i') i = 1;
        else if (*m == 'l') l = 1;
        else if (*m == 'c') c = 1;
        else if (*m == 'q') q = 1;
        else { char bad[2] = {*m, 0}; return error("invalid option: -", bad); }
    }
    (void)q;
    switch (op) {
    case 'S':
        if (c) {
            if (need_root()) return 1;
            barnix->rm("/etc/brm/sync");
            line(15, T("Removed the downloaded package list."));
            return 0;
        }
        if (y) { if (need_root() || sync_databases()) return 1; }
        if (s) return search(count ? targets[0] : "", 0);
        if (i) return count ? info(targets, count, 0) : error("no targets specified", 0);
        if (l) return list_repo();
        if (u) return upgrade();
        if (count) return install(targets, count, 0);
        return y ? 0 : error("no targets specified (use -h for help)", 0);
    case 'R':
        return count ? remove_packages(targets, count) : error("no targets specified (use -h for help)", 0);
    case 'Q':
        if (s) return search(count ? targets[0] : "", 1);
        if (i) return count ? info(targets, count, 1) : error("no targets specified", 0);
        if (l) return count ? list_files(targets, count) : error("no targets specified", 0);
        return query(targets, count, u);
    case 'U':
        if (count != 1) return error("brm -U needs one program file", 0);
        return install_file(targets[0]);
    }
    return error("invalid operation: -", operation);
}
