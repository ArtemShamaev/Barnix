/* git clone URL [DIRECTORY]: fetches the newest commit of a repository over
 * the smart HTTP protocol (like git clone --depth 1) and writes its files.
 * Steps: GET info/refs for HEAD, POST git-upload-pack with "want" and
 * "deepen 1", read the pack from side band 1, inflate the objects, apply
 * deltas, hash them (SHA-1) and walk the commit's tree. No history is kept:
 * Barnix has no git repository format, only the working files. */
#include "command.h"
#include <barnix_net.h>
#include "../BDK/runtime/bnet.h"

#define FILE_LIMIT (640U * 1024U)
typedef struct { unsigned int offset, size, base; int type; unsigned char *data; unsigned char sha[20]; int resolved, next; } Object;
enum { COMMIT = 1, TREE = 2, BLOB = 3, TAG = 4, OFS_DELTA = 6, REF_DELTA = 7 };

static unsigned char *arena;
static unsigned int arena_used, arena_size;
static void *take(unsigned int bytes)
{
    bytes = (bytes + 15) & ~15U;
    if (bytes > arena_size - arena_used) return 0;
    void *p = arena + arena_used;
    arena_used += bytes;
    return p;
}
static void say(int color, const char *a, const char *b)
{
    barnix->print(color, a);
    if (b) barnix->print(color, b);
    barnix->puts("");
}
static void number(int color, unsigned int value)
{
    char text[12]; int n = 0;
    do text[n++] = (char)('0' + value % 10); while (value /= 10);
    char out[12]; int k = 0;
    while (n) out[k++] = text[--n];
    out[k] = 0;
    barnix->print(color, out);
}
static int same(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static int hexval(int c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; }
static int unhex(const char *text, unsigned char out[20])
{
    for (int i = 0; i < 20; i++) {
        int a = hexval(text[2 * i]), b = hexval(text[2 * i + 1]);
        if (a < 0 || b < 0) return -1;
        out[i] = (unsigned char)(a * 16 + b);
    }
    return 0;
}
static void tohex(const unsigned char *sha, char *out, int bytes)
{
    for (int i = 0; i < bytes; i++) { out[2 * i] = "0123456789abcdef"[sha[i] >> 4]; out[2 * i + 1] = "0123456789abcdef"[sha[i] & 15]; }
    out[2 * bytes] = 0;
}

/* ------------------------------------------------------------ HTTP bodies */
typedef struct { unsigned char *data; unsigned int length, capacity; } Buffer;
static int collect(void *context, const void *data, unsigned int length)
{
    Buffer *b = context;
    if (b->length + length > b->capacity) return 1;
    memcpy(b->data + b->length, data, length);
    b->length += length;
    return 0;
}
/* The upload-pack answer: pkt-lines; band 1 carries the pack. */
typedef struct { unsigned char line[65536 + 4]; unsigned int have, need; Buffer *pack; int error; unsigned int marks; } PktStream;
static int pkt_feed(void *context, const void *data, unsigned int length)
{
    PktStream *s = context;
    const unsigned char *p = data;
    while (length) {
        unsigned int want = s->need ? s->need : 4;
        unsigned int part = want - s->have < length ? want - s->have : length;
        memcpy(s->line + s->have, p, part);
        s->have += part; p += part; length -= part;
        if (s->have < want) break;
        if (!s->need) {
            unsigned int size = 0;
            for (int i = 0; i < 4; i++) { int v = hexval(s->line[i] | 32); if (v < 0) { s->error = 1; return 1; } size = size * 16 + (unsigned int)v; }
            s->have = 0;
            if (size <= 4) continue;            /* flush or delimiter */
            if (size > sizeof(s->line)) { s->error = 1; return 1; }
            s->need = size - 4;
            continue;
        }
        unsigned int size = s->need;
        s->need = 0; s->have = 0;
        if (s->line[0] == 1) {
            if (collect(s->pack, s->line + 1, size - 1)) { s->error = 2; return 1; }
            while (s->pack->length / 65536 > s->marks) { barnix->print(10, "#"); s->marks++; }
        } else if (s->line[0] == 3) {
            s->line[size < sizeof(s->line) ? size : sizeof(s->line) - 1] = 0;
            barnix->puts("");
            say(4, "remote: ", (const char *)s->line + 1);
            s->error = 3;
            return 1;
        }
        /* band 2 progress, NAK, ACK and shallow lines are not needed */
    }
    return 0;
}

/* ------------------------------------------------------------ pack */
static Object *objects;
static unsigned int count;
static int buckets[4096];
static void object_hash(Object *o)
{
    static const char *const names[5] = {"", "commit", "tree", "blob", "tag"};
    char header[32];
    unsigned int n = 0;
    for (const char *t = names[o->type]; *t; t++) header[n++] = *t;
    header[n++] = ' ';
    char digits[12]; int k = 0;
    unsigned int v = o->size;
    do digits[k++] = (char)('0' + v % 10); while (v /= 10);
    while (k) header[n++] = digits[--k];
    header[n++] = 0;
    Sha1 s;
    sha1_init(&s); sha1_update(&s, header, n); sha1_update(&s, o->data, o->size); sha1_final(&s, o->sha);
    unsigned int b = (unsigned int)(o->sha[0] << 4 | o->sha[1] >> 4);
    o->next = buckets[b]; buckets[b] = (int)(o - objects);
}
static Object *find(const unsigned char sha[20])
{
    for (int i = buckets[(unsigned int)(sha[0] << 4 | sha[1] >> 4)]; i >= 0; i = objects[i].next)
        if (!memcmp(objects[i].sha, sha, 20)) return &objects[i];
    return 0;
}
static unsigned int varint(const unsigned char **p, const unsigned char *end)
{
    unsigned int v = 0, shift = 0;
    while (*p < end) { unsigned char c = *(*p)++; v |= (unsigned int)(c & 0x7f) << shift; shift += 7; if (!(c & 0x80)) break; }
    return v;
}
static int resolve(Object *o, int depth);
static int apply_delta(Object *o, Object *base)
{
    const unsigned char *d = o->data, *end = o->data + o->size;
    unsigned int source = varint(&d, end), target = varint(&d, end);
    if (source != base->size) return -1;
    unsigned char *out = take(target + 1);
    if (!out) return -1;
    unsigned int n = 0;
    while (d < end) {
        unsigned char op = *d++;
        if (op & 0x80) {
            unsigned int offset = 0, size = 0;
            for (int i = 0; i < 4; i++) if (op & (1 << i)) { if (d >= end) return -1; offset |= (unsigned int)*d++ << (8 * i); }
            for (int i = 0; i < 3; i++) if (op & (0x10 << i)) { if (d >= end) return -1; size |= (unsigned int)*d++ << (8 * i); }
            if (!size) size = 0x10000;
            if (offset + size > base->size || n + size > target) return -1;
            memcpy(out + n, base->data + offset, size); n += size;
        } else if (op) {
            if (d + op > end || n + op > target) return -1;
            memcpy(out + n, d, op); n += op; d += op;
        } else return -1;
    }
    if (n != target) return -1;
    o->data = out; o->size = target; o->type = base->type;
    return 0;
}
static int resolve(Object *o, int depth)
{
    if (o->resolved) return o->resolved > 0 ? 0 : -1;
    if (depth > 64) return -1;
    o->resolved = -1;
    Object *base = 0;
    if (o->type == OFS_DELTA) {
        for (unsigned int i = 0; i < count && !base; i++) if (objects[i].offset == o->base) base = &objects[i];
    } else if (o->type == REF_DELTA) {
        for (unsigned int i = 0; i < count && !base; i++) {
            if (objects[i].type < OFS_DELTA && !objects[i].resolved) { objects[i].resolved = 1; object_hash(&objects[i]); }
            if (objects[i].resolved > 0 && !memcmp(objects[i].sha, (unsigned char *)o->base, 20)) base = &objects[i];
        }
    }
    if (o->type >= OFS_DELTA) {
        if (!base || resolve(base, depth + 1) || apply_delta(o, base)) return -1;
    }
    o->resolved = 1;
    object_hash(o);
    return 0;
}
static int parse_pack(const unsigned char *pack, unsigned int length)
{
    if (length < 32 || memcmp(pack, "PACK", 4) || pack[7] != 2) { say(4, "the server sent no pack", 0); return -1; }
    count = (unsigned int)pack[8] << 24 | (unsigned int)pack[9] << 16 | (unsigned int)pack[10] << 8 | pack[11];
    objects = take(count * sizeof(Object) + 16);
    if (!objects) { say(4, "not enough memory for ", "the object list"); return -1; }
    for (int i = 0; i < 4096; i++) buckets[i] = -1;
    unsigned int at = 12;
    for (unsigned int i = 0; i < count; i++) {
        Object *o = &objects[i];
        memset(o, 0, sizeof(*o));
        o->offset = at;
        if (at >= length) return -1;
        unsigned char c = pack[at++];
        o->type = c >> 4 & 7;
        unsigned int size = c & 15, shift = 4;
        while (c & 0x80) { if (at >= length) return -1; c = pack[at++]; size |= (unsigned int)(c & 0x7f) << shift; shift += 7; }
        if (o->type == OFS_DELTA) {
            unsigned int back = 0;
            if (at >= length) return -1;
            c = pack[at++]; back = c & 0x7f;
            while (c & 0x80) { if (at >= length) return -1; c = pack[at++]; back = ((back + 1) << 7) | (c & 0x7f); }
            if (back > o->offset) return -1;
            o->base = o->offset - back;
        } else if (o->type == REF_DELTA) {
            if (at + 20 > length) return -1;
            unsigned char *sha = take(20);
            if (!sha) return -1;
            memcpy(sha, pack + at, 20); at += 20;
            o->base = (unsigned int)sha;
        } else if (o->type < COMMIT || o->type > TAG) return -1;
        o->data = take(size + 1);
        if (!o->data) { say(4, "not enough memory for the repository", 0); return -1; }
        unsigned long used = 0;
        long n = bnet_inflate(pack + at, length - at, &used, o->data, size + 1, 1);
        if (n != (long)size) { say(4, "a damaged object in the pack", 0); return -1; }
        o->size = size;
        at += (unsigned int)used;
        if (o->type < OFS_DELTA) { o->resolved = 1; object_hash(o); }
    }
    for (unsigned int i = 0; i < count; i++)
        if (resolve(&objects[i], 0)) { say(4, "cannot rebuild an object from its delta", 0); return -1; }
    return 0;
}

/* ------------------------------------------------------------ checkout */
static unsigned int files, directories, skipped;
static int checkout(const unsigned char sha[20], char *path, unsigned int length, int depth)
{
    Object *tree = find(sha);
    if (!tree || tree->type != TREE || depth > 32) { say(4, "the tree is missing from the pack: ", path); return -1; }
    const unsigned char *p = tree->data, *end = tree->data + tree->size;
    while (p < end) {
        unsigned int mode = 0;
        while (p < end && *p != ' ') mode = mode * 8 + (unsigned int)(*p++ - '0');
        const unsigned char *name = ++p;
        while (p < end && *p) p++;
        unsigned int name_length = (unsigned int)(p - name);
        p++;
        if (p + 20 > end) return -1;
        const unsigned char *id = p;
        p += 20;
        if (length + 1 + name_length + 1 > 512) { skipped++; continue; }
        path[length] = '/';
        memcpy(path + length + 1, name, name_length);
        unsigned int full = length + 1 + name_length;
        path[full] = 0;
        if (mode == 040000) {
            if (barnix->mkdir(path) && barnix->stat(path) < 0) { say(4, "cannot create ", path); return -1; }
            directories++;
            if (checkout(id, path, full, depth + 1)) return -1;
        } else if (mode == 0160000) {
            skipped++;                       /* submodule */
        } else {
            Object *blob = find(id);
            if (!blob || blob->type != BLOB) { say(4, "a file is missing from the pack: ", path); return -1; }
            if (blob->size > FILE_LIMIT) { say(14, "skipped (larger than 640 KiB): ", path); skipped++; }
            else if (barnix->write(path, (const char *)blob->data, (int)blob->size)) {
                say(4, "cannot write ", path);
                say(14, "The disk may be full: the live CD keeps files in a small RAM disk; install Barnix or mount a disk.", 0);
                return -1;
            }
            else files++;
        }
        path[length] = 0;
    }
    return 0;
}

/* info/refs: only the first lines matter (HEAD and its branch); big
 * repositories list thousands of refs, so stop reading after HEAD. */
typedef struct { char line[1024]; unsigned int have, need; int have_head; unsigned char head[20]; char branch[128]; } RefStream;
static void ref_line(RefStream *r, const char *line, unsigned int length)
{
    if (length < 45 || line[40] != ' ') return;
    int is_head = !memcmp(line + 41, "HEAD", 4);
    if (!is_head && (r->have_head || memcmp(line + 41, "refs/heads/", 11))) return;
    if (unhex(line, r->head)) return;
    r->have_head = is_head ? 1 : 2;
    for (unsigned int i = 45; i + 23 < length; i++)
        if (!memcmp(line + i, "symref=HEAD:refs/heads/", 23)) {
            unsigned int k = 0;
            for (const char *b = line + i + 23; b < line + length && *b != ' ' && *b != '\n' && k + 1 < sizeof(r->branch); b++) r->branch[k++] = *b;
            r->branch[k] = 0;
        }
}
static int ref_feed(void *context, const void *data, unsigned int length)
{
    RefStream *r = context;
    const unsigned char *p = data;
    while (length) {
        unsigned int want = r->need ? r->need : 4;
        unsigned int part = want - r->have < length ? want - r->have : length;
        if (r->have + part <= sizeof(r->line)) memcpy(r->line + r->have, p, part);
        r->have += part; p += part; length -= part;
        if (r->have < want) break;
        if (!r->need) {
            unsigned int size = 0;
            for (int i = 0; i < 4; i++) { int v = hexval(r->line[i] | 32); if (v < 0) return 1; size = size * 16 + (unsigned int)v; }
            r->have = 0;
            if (size > 4) r->need = size - 4;
            continue;
        }
        unsigned int size = r->need < sizeof(r->line) ? r->need : sizeof(r->line);
        r->need = 0; r->have = 0;
        ref_line(r, r->line, size);
        if (r->have_head == 1) return 1;   /* enough */
    }
    return 0;
}

int main(int argc, const char *const *argv)
{
    if (argc < 3 || argc > 4 || !same(argv[1], "clone")) return fail("usage: git clone https://host/user/repo [DIRECTORY]");
    if (barnix->version < 12) return fail("git needs a Barnix kernel with networking (ABI 12)");
    static char base[400], url[512], directory[256], path[520];
    unsigned int n = 0;
    while (argv[2][n] && n + 1 < sizeof(base)) { base[n] = argv[2][n]; n++; }
    while (n && base[n - 1] == '/') n--;
    base[n] = 0;
    if (argc == 4) {
        unsigned int k = 0;
        while (argv[3][k] && k + 1 < sizeof(directory)) { directory[k] = argv[3][k]; k++; }
        directory[k] = 0;
    } else {
        const char *name = base;
        for (const char *p = base; *p; p++) if (*p == '/') name = p + 1;
        unsigned int k = 0;
        while (name[k] && k + 1 < sizeof(directory)) { directory[k] = name[k]; k++; }
        if (k > 4 && same(directory + k - 4, ".git")) k -= 4;
        directory[k] = 0;
    }
    if (!directory[0]) return fail("cannot tell the directory name; give it after the URL");
    if (barnix->stat(directory) >= 0) { say(4, "already exists: ", directory); return 1; }
    arena_size = 0;
    arena = barnix->memory(&arena_size);
    if (!arena || arena_size < (8U << 20)) return fail("not enough memory (8 MiB needed)");
    arena_used = 0;

    /* 1. the references */
    barnix->print(15, "Cloning into "); barnix->print(11, directory); barnix->puts("...");
    static BnetRequest request;
    static RefStream refs;
    memcpy(url, base, n); memcpy(url + n, "/info/refs?service=git-upload-pack", 35);
    request.data = ref_feed; request.context = &refs;
    if (bnet_request(url, &request) && !refs.have_head) { say(4, "cannot reach the repository: ", request.error); return 1; }
    unsigned char *head = refs.head;
    int have_head = refs.have_head;
    char *branch = refs.branch;
    if (!have_head) return fail("the repository is empty or not a git repository");

    /* 2. the pack of the newest commit */
    static char body[256];
    char hex[41];
    tohex(head, hex, 20);
    unsigned int b = 0;
    const char *want = "want ";
    const char *capabilities = " side-band-64k ofs-delta shallow no-progress agent=git/barnix-1.0\n";
    unsigned int line = 4 + 5 + 40 + (unsigned int)strlen(capabilities);
    body[b++] = "0123456789abcdef"[line >> 12 & 15]; body[b++] = "0123456789abcdef"[line >> 8 & 15];
    body[b++] = "0123456789abcdef"[line >> 4 & 15]; body[b++] = "0123456789abcdef"[line & 15];
    memcpy(body + b, want, 5); b += 5; memcpy(body + b, hex, 40); b += 40;
    memcpy(body + b, capabilities, strlen(capabilities)); b += (unsigned int)strlen(capabilities);
    memcpy(body + b, "000ddeepen 1\n0000" "0009done\n", 4 + 9 + 4 + 9); b += 26;
    memcpy(url + n, "/git-upload-pack", 17);
    Buffer pack = {0, 0, 0};
    pack.capacity = (arena_size - arena_used) / 2;
    pack.data = take(pack.capacity);
    static PktStream stream;
    stream.pack = &pack;
    memset(&request, 0, sizeof(request));
    request.method = "POST";
    request.content_type = "application/x-git-upload-pack-request";
    request.accept = "application/x-git-upload-pack-result";
    request.body = body; request.body_length = b;
    request.data = pkt_feed; request.context = &stream;
    barnix->print(15, "Receiving ");
    int result = bnet_request(url, &request);
    barnix->puts("");
    if (stream.error == 2) return fail("the repository is too large for the memory of this computer");
    if (stream.error || result) { if (!stream.error) say(4, "download failed: ", request.error); return 1; }
    barnix->print(15, "Unpacking "); number(15, pack.length / 1024); barnix->println(15, " KiB...");
    /* the rest of the arena holds the objects */
    if (parse_pack(pack.data, pack.length)) return 1;

    /* 3. the files */
    Object *commit = find(head);
    if (!commit || commit->type != COMMIT || commit->size < 45 || memcmp(commit->data, "tree ", 5)) return fail("the commit is missing from the pack");
    unsigned char tree[20];
    if (unhex((const char *)commit->data + 5, tree)) return fail("damaged commit");
    if (barnix->mkdir(directory)) { say(4, "cannot create ", directory); return 1; }
    unsigned int length = (unsigned int)strlen(directory);
    memcpy(path, directory, length + 1);
    if (checkout(tree, path, length, 0)) return 1;
    barnix->print(10, "Done: ");
    number(10, files); barnix->print(10, " files, "); number(10, directories); barnix->print(10, " folders");
    if (skipped) { barnix->print(14, ", skipped "); number(14, skipped); }
    barnix->print(10, "; commit "); hex[12] = 0; barnix->print(10, hex);
    if (branch[0]) { barnix->print(10, " on "); barnix->print(10, branch); }
    barnix->puts("");
    return 0;
}
