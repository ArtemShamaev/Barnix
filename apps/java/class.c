/* Class loading: the class path (directories, jars, the built-in class
 * library), class file parsing, linking (field layout, vtables, interface
 * closure), initialization and constant pool resolution. One loader. */
#include "jvm.h"
#include <stdio.h>

long bnet_inflate(const void *in, unsigned long in_length, unsigned long *used, void *out, unsigned long capacity, int zlib);

/* ---- symbols ---- */
typedef struct Symbol { struct Symbol *next; uint32_t hash, length; char text[]; } Symbol;
#define SYMBOL_BUCKETS 8192
static Symbol *symbols[SYMBOL_BUCKETS];
static uint32_t hash_bytes(const char *s, uint32_t n)
{
    uint32_t h = 2166136261U;
    while (n--) h = (h ^ (uint8_t)*s++) * 16777619U;
    return h;
}
const char *symbol(const char *s, uint32_t n)
{
    uint32_t h = hash_bytes(s, n);
    for (Symbol *y = symbols[h % SYMBOL_BUCKETS]; y; y = y->next)
        if (y->hash == h && y->length == n && !memcmp(y->text, s, n)) return y->text;
    Symbol *y = meta_alloc(sizeof(Symbol) + n + 1);
    y->hash = h; y->length = n;
    memcpy(y->text, s, n);
    y->next = symbols[h % SYMBOL_BUCKETS];
    symbols[h % SYMBOL_BUCKETS] = y;
    return y->text;
}
static const char *sym(const char *s) { return symbol(s, (uint32_t)strlen(s)); }

/* ---- class path ---- */
typedef struct ZipEntry { struct ZipEntry *next; const char *name; uint32_t method, csize, size, offset; } ZipEntry;
typedef struct PathEntry {
    struct PathEntry *next;
    char *dir;                   /* directory entry, or null for a zip */
    const uint8_t *zip;
    uint32_t zip_size, bucket_count;
    ZipEntry **buckets;
} PathEntry;
static PathEntry *path_first, **path_last = &path_first;

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static int zip_open(PathEntry *e)
{
    const uint8_t *z = e->zip;
    uint32_t n = e->zip_size;
    if (n < 22) return -1;
    uint32_t end = n - 22, stop = n > 22 + 65535 ? n - 22 - 65535 : 0;
    while (le32(z + end) != 0x06054b50U) { if (end == stop) return -1; end--; }
    uint32_t count = le16(z + end + 10), at = le32(z + end + 16);
    e->bucket_count = count * 2 + 1;
    e->buckets = meta_alloc(e->bucket_count * sizeof(ZipEntry *));
    for (uint32_t i = 0; i < count; i++) {
        if (at + 46 > n || le32(z + at) != 0x02014b50U) return -1;
        uint32_t name_length = le16(z + at + 28), extra = le16(z + at + 30), comment = le16(z + at + 32);
        ZipEntry *x = meta_alloc(sizeof(ZipEntry));
        x->name = symbol((const char *)z + at + 46, name_length);
        x->method = le16(z + at + 10);
        x->csize = le32(z + at + 20);
        x->size = le32(z + at + 24);
        x->offset = le32(z + at + 42);
        uint32_t h = hash_bytes(x->name, name_length) % e->bucket_count;
        x->next = e->buckets[h];
        e->buckets[h] = x;
        at += 46 + name_length + extra + comment;
    }
    return 0;
}

static uint8_t *zip_read(PathEntry *e, const char *name, uint32_t *size)
{
    uint32_t length = (uint32_t)strlen(name), h = hash_bytes(name, length) % e->bucket_count;
    for (ZipEntry *x = e->buckets[h]; x; x = x->next) {
        if (strcmp(x->name, name)) continue;
        const uint8_t *local = e->zip + x->offset;
        if (x->offset + 30 > e->zip_size || le32(local) != 0x04034b50U) return 0;
        const uint8_t *data = local + 30 + le16(local + 26) + le16(local + 28);
        if (data + x->csize > e->zip + e->zip_size) return 0;
        uint8_t *out = vm_malloc(x->size + 1);
        if (!out) return 0;
        if (x->method == 0) memcpy(out, data, x->size);
        else if (x->method == 8) {
            if (bnet_inflate(data, x->csize, 0, out, x->size, 0) != (long)x->size) { vm_free(out); return 0; }
        } else { vm_free(out); return 0; }
        out[x->size] = 0;
        *size = x->size;
        return out;
    }
    return 0;
}

void classpath_add_memory(const uint8_t *zip, uint32_t size)
{
    PathEntry *e = meta_alloc(sizeof(PathEntry));
    e->zip = zip; e->zip_size = size;
    if (zip_open(e)) { vm_printf(1, "bjvm: bad built-in archive\n"); return; }
    *path_last = e; path_last = &e->next;
}

void classpath_add(const char *path)
{
    PathEntry *e = meta_alloc(sizeof(PathEntry));
    if (plat_is_dir(path)) e->dir = meta_strdup(path);
    else {
        int size = plat_file_size(path);
        if (size < 0) { if (vm_verbose) vm_printf(1, "bjvm: class path entry %s not found\n", path); return; }
        uint8_t *data = vm_malloc((uint32_t)size + 1);
        if (!data || plat_file_read(path, 0, data, (uint32_t)size) != size) { vm_printf(1, "bjvm: cannot read %s\n", path); return; }
        e->zip = data; e->zip_size = (uint32_t)size;
        if (zip_open(e)) { vm_printf(1, "bjvm: %s is not a jar or zip file\n", path); return; }
    }
    *path_last = e; path_last = &e->next;
}

uint8_t *resource_read(const char *name, uint32_t *size)
{
    while (*name == '/') name++;
    for (PathEntry *e = path_first; e; e = e->next) {
        if (e->zip) {
            uint8_t *data = zip_read(e, name, size);
            if (data) return data;
        } else {
            char full[512];
            snprintf(full, sizeof(full), "%s/%s", e->dir, name);
            int n = plat_file_size(full);
            if (n < 0 || plat_is_dir(full)) continue;
            uint8_t *data = vm_malloc((uint32_t)n + 1);
            if (data && plat_file_read(full, 0, data, (uint32_t)n) == n) { data[n] = 0; *size = (uint32_t)n; return data; }
            vm_free(data);
        }
    }
    return 0;
}

/* ---- loaded classes ---- */
#define CLASS_BUCKETS 4096
static Class *classes[CLASS_BUCKETS];
static uint32_t class_ids;
Class *find_loaded(const char *name)
{
    uint32_t h = hash_bytes(name, (uint32_t)strlen(name)) % CLASS_BUCKETS;
    for (Class *c = classes[h]; c; c = c->hash_next) if (!strcmp(c->name, name)) return c;
    return 0;
}
static void add_class(Class *c)
{
    uint32_t h = hash_bytes(c->name, (uint32_t)strlen(c->name)) % CLASS_BUCKETS;
    c->hash_next = classes[h];
    classes[h] = c;
    c->id = ++class_ids;
}
void class_iterate(void (*fn)(Class *c, void *arg), void *arg)
{
    for (unsigned i = 0; i < CLASS_BUCKETS; i++)
        for (Class *c = classes[i]; c; c = c->hash_next) fn(c, arg);
}

int count_slots(const char *desc, char *ret)
{
    int n = 0;
    const char *p = desc + 1;
    while (*p && *p != ')') {
        if (*p == 'J' || *p == 'D') { n += 2; p++; continue; }
        n++;
        while (*p == '[') p++;
        if (*p == 'L') { while (*p && *p != ';') p++; }
        if (*p) p++;
    }
    if (ret) { char r = p[0] ? p[1] : 'V'; *ret = r == '[' ? 'L' : r; }
    return n;
}

static uint8_t kind_of(char d)
{
    switch (d) {
    case 'B': return K_BYTE; case 'C': return K_CHAR; case 'S': return K_SHORT; case 'Z': return K_BOOL;
    case 'J': case 'D': return K_WIDE; case 'L': case '[': return K_REF; case 'F': return K_FLOAT;
    default: return K_INT;
    }
}
static uint32_t kind_size(uint8_t k) { return k == K_WIDE ? 8 : k == K_BYTE || k == K_BOOL ? 1 : k == K_CHAR || k == K_SHORT ? 2 : 4; }

const char *class_java_name(Class *c, char *buffer, uint32_t size)
{
    uint32_t i = 0;
    for (const char *p = c->name; *p && i + 1 < size; p++) buffer[i++] = *p == '/' ? '.' : *p;
    buffer[i] = 0;
    return buffer;
}

/* ---- parsing ---- */
typedef struct { const uint8_t *p, *end; int bad; } Reader;
static uint8_t r8(Reader *r) { if (r->p + 1 > r->end) { r->bad = 1; return 0; } return *r->p++; }
static uint16_t r16(Reader *r) { if (r->p + 2 > r->end) { r->bad = 1; return 0; } uint16_t v = (uint16_t)(r->p[0] << 8 | r->p[1]); r->p += 2; return v; }
static uint32_t r32(Reader *r) { if (r->p + 4 > r->end) { r->bad = 1; return 0; } uint32_t v = (uint32_t)r->p[0] << 24 | r->p[1] << 16 | r->p[2] << 8 | r->p[3]; r->p += 4; return v; }
static const char *cp_utf8(ConstPool *cp, uint16_t i) { return i && i < cp->count && cp->tags[i] == CP_UTF8 ? (const char *)(uintptr_t)cp->data[i] : ""; }
static const char *cp_class_name(ConstPool *cp, uint16_t i) { return i && i < cp->count && cp->tags[i] == CP_CLASS ? cp_utf8(cp, (uint16_t)cp->data[i]) : 0; }

static Class *parse_class(Thread *t, const uint8_t *bytes, uint32_t size, const char *expected)
{
    Reader r = {bytes, bytes + size, 0};
    if (r32(&r) != 0xcafebabeU) { throw_new(t, "java/lang/ClassFormatError", expected); return 0; }
    r16(&r);
    uint16_t major = r16(&r);
    if (major < 45 || major > 52) { throw_new(t, "java/lang/UnsupportedClassVersionError", expected); return 0; }
    Class *c = meta_alloc(sizeof(Class));
    ConstPool *cp = &c->cp;
    cp->count = r16(&r);
    cp->tags = meta_alloc(cp->count);
    cp->data = meta_alloc(cp->count * 4U);
    cp->resolved = meta_alloc(cp->count * sizeof(void *));
    for (uint16_t i = 1; i < cp->count && !r.bad; i++) {
        uint8_t tag = r8(&r);
        cp->tags[i] = tag;
        switch (tag) {
        case CP_UTF8: {
            uint16_t n = r16(&r);
            if (r.p + n > r.end) { r.bad = 1; break; }
            cp->data[i] = (uint32_t)(uintptr_t)symbol((const char *)r.p, n);
            r.p += n;
            break;
        }
        case CP_INT: case CP_FLOAT: cp->data[i] = r32(&r); break;
        case CP_LONG: case CP_DOUBLE:
            cp->data[i] = r32(&r);
            if (i + 1 < cp->count) cp->data[i + 1] = r32(&r);
            i++;
            break;
        case CP_CLASS: case CP_STRING: case CP_MTYPE: cp->data[i] = r16(&r); break;
        case CP_FIELD: case CP_METHOD: case CP_IMETHOD: case CP_NAMETYPE: case CP_INDY:
            cp->data[i] = (uint32_t)r16(&r) << 16; cp->data[i] |= r16(&r); break;
        case CP_HANDLE: r8(&r); cp->data[i] = r16(&r); break;
        default: r.bad = 1;
        }
    }
    if (r.bad) { throw_new(t, "java/lang/ClassFormatError", expected); return 0; }
    cp->cache = meta_alloc(cp->count * sizeof(InlineCache));
    c->access = r16(&r);
    c->name = cp_class_name(cp, r16(&r));
    uint16_t super_index = r16(&r);
    const char *super_name = super_index ? cp_class_name(cp, super_index) : 0;
    if (!c->name || (expected && strcmp(c->name, expected))) {
        throw_new(t, "java/lang/NoClassDefFoundError", expected);
        return 0;
    }
    c->interface_count = r16(&r);
    const char **interface_names = c->interface_count ? meta_alloc(c->interface_count * sizeof(char *)) : 0;
    for (uint16_t i = 0; i < c->interface_count; i++) interface_names[i] = cp_class_name(cp, r16(&r));
    c->field_count = r16(&r);
    c->fields = meta_alloc((c->field_count + 1U) * sizeof(Field));
    for (uint16_t i = 0; i < c->field_count && !r.bad; i++) {
        Field *f = &c->fields[i];
        f->owner = c;
        f->access = r16(&r);
        f->name = cp_utf8(cp, r16(&r));
        f->desc = cp_utf8(cp, r16(&r));
        f->kind = kind_of(f->desc[0]);
        for (uint16_t a = r16(&r); a-- && !r.bad;) {
            const char *name = cp_utf8(cp, r16(&r));
            uint32_t length = r32(&r);
            if (!strcmp(name, "ConstantValue") && length == 2) f->constant = r16(&r);
            else r.p += length;
        }
    }
    c->method_count = r16(&r);
    c->methods = meta_alloc((c->method_count + 1U) * sizeof(Method));
    for (uint16_t i = 0; i < c->method_count && !r.bad; i++) {
        Method *m = &c->methods[i];
        m->owner = c;
        m->access = r16(&r);
        m->name = cp_utf8(cp, r16(&r));
        m->desc = cp_utf8(cp, r16(&r));
        m->arg_slots = (uint16_t)(count_slots(m->desc, &m->ret) + !(m->access & ACC_STATIC));
        m->vtable_index = -1;
        for (uint16_t a = r16(&r); a-- && !r.bad;) {
            const char *name = cp_utf8(cp, r16(&r));
            uint32_t length = r32(&r);
            const uint8_t *next = r.p + length;
            if (next > r.end) { r.bad = 1; break; }
            if (!strcmp(name, "Code")) {
                m->max_stack = r16(&r);
                m->max_locals = r16(&r);
                m->code_length = r32(&r);
                if (r.p + m->code_length > next) { r.bad = 1; break; }
                m->code = meta_alloc(m->code_length + 4);
                memcpy(m->code, r.p, m->code_length);
                r.p += m->code_length;
                m->handler_count = r16(&r);
                m->handlers = m->handler_count ? meta_alloc(m->handler_count * sizeof(Handler)) : 0;
                for (uint16_t h = 0; h < m->handler_count; h++) {
                    m->handlers[h].start = r16(&r); m->handlers[h].end = r16(&r);
                    m->handlers[h].handler = r16(&r); m->handlers[h].type = r16(&r);
                }
                for (uint16_t b = r16(&r); b-- && !r.bad;) {
                    const char *an = cp_utf8(cp, r16(&r));
                    uint32_t al = r32(&r);
                    if (!strcmp(an, "LineNumberTable") && !m->lines) {
                        m->line_count = r16(&r);
                        m->lines = meta_alloc(m->line_count * sizeof(LineEntry) + 1);
                        for (uint16_t l = 0; l < m->line_count; l++) { m->lines[l].pc = r16(&r); m->lines[l].line = r16(&r); }
                    } else r.p += al;
                }
            }
            r.p = next;
        }
        if (m->max_locals < m->arg_slots) m->max_locals = m->arg_slots;
    }
    for (uint16_t a = r16(&r); a-- && !r.bad;) {
        const char *name = cp_utf8(cp, r16(&r));
        uint32_t length = r32(&r);
        if (!strcmp(name, "SourceFile") && length == 2) c->source_file = cp_utf8(cp, r16(&r));
        else r.p += length;
    }
    if (r.bad) { throw_new(t, "java/lang/ClassFormatError", expected); return 0; }

    /* Link: superclass and interfaces first. */
    if (super_name) {
        c->super = load_class(t, super_name);
        if (!c->super) return 0;
        if (c->super->access & ACC_INTERFACE) { throw_new(t, "java/lang/IncompatibleClassChangeError", c->name); return 0; }
    }
    if (c->interface_count) c->interfaces = meta_alloc(c->interface_count * sizeof(Class *));
    for (uint16_t i = 0; i < c->interface_count; i++) {
        c->interfaces[i] = interface_names[i] ? load_class(t, interface_names[i]) : 0;
        if (!c->interfaces[i]) return 0;
    }
    return c;
}

static void add_interface(Class **list, uint16_t *count, uint32_t capacity, Class *i)
{
    for (uint16_t k = 0; k < *count; k++) if (list[k] == i) return;
    if (*count < capacity) list[(*count)++] = i;
    for (uint16_t k = 0; k < i->interface_count; k++) add_interface(list, count, capacity, i->interfaces[k]);
}

static void link_class(Class *c)
{
    Class *s = c->super;
    /* Interface closure. */
    uint32_t capacity = (s ? s->all_interface_count : 0) + 1;
    for (uint16_t i = 0; i < c->interface_count; i++) capacity += c->interfaces[i]->all_interface_count + 1;
    c->all_interfaces = meta_alloc(capacity * sizeof(Class *));
    if (s) for (uint16_t i = 0; i < s->all_interface_count; i++) add_interface(c->all_interfaces, &c->all_interface_count, capacity, s->all_interfaces[i]);
    for (uint16_t i = 0; i < c->interface_count; i++) add_interface(c->all_interfaces, &c->all_interface_count, capacity, c->interfaces[i]);

    /* Fields: wide, then words (refs among them), then halves, then bytes. */
    uint32_t size = s ? s->instance_size : sizeof(Object), statics = 0;
    uint32_t refs = s ? s->ref_count : 0, static_refs = 0;
    for (uint16_t i = 0; i < c->field_count; i++) {
        if (c->fields[i].kind != K_REF) continue;
        if (c->fields[i].access & ACC_STATIC) static_refs++; else refs++;
    }
    c->refs = refs ? meta_alloc(refs * 2) : 0;
    if (s && s->ref_count) memcpy(c->refs, s->refs, s->ref_count * 2);
    c->ref_count = s ? s->ref_count : 0;
    c->static_refs = static_refs ? meta_alloc(static_refs * 2) : 0;
    for (uint32_t want = 8; want >= 1; want /= 2) {
        for (uint16_t i = 0; i < c->field_count; i++) {
            Field *f = &c->fields[i];
            if (kind_size(f->kind) != want) continue;
            if (f->access & ACC_STATIC) {
                statics = (statics + want - 1) & ~(want - 1);
                f->offset = statics; statics += want;
                if (f->kind == K_REF) c->static_refs[c->static_ref_count++] = (uint16_t)f->offset;
            } else {
                size = (size + want - 1) & ~(want - 1);
                f->offset = size; size += want;
                if (f->kind == K_REF) c->refs[c->ref_count++] = (uint16_t)f->offset;
            }
        }
    }
    c->instance_size = (size + 7) & ~7U;
    c->statics_size = statics;
    c->statics = statics ? meta_alloc(statics) : 0;

    /* Virtual table. */
    uint32_t vsize = s ? s->vtable_size : 0;
    c->vtable = meta_alloc((vsize + c->method_count + 1) * sizeof(Method *));
    if (vsize) memcpy(c->vtable, s->vtable, vsize * sizeof(Method *));
    if (!(c->access & ACC_INTERFACE))
        for (uint16_t i = 0; i < c->method_count; i++) {
            Method *m = &c->methods[i];
            if ((m->access & (ACC_STATIC | ACC_PRIVATE)) || m->name[0] == '<') continue;
            uint32_t k = 0;
            while (k < vsize && !(c->vtable[k]->name == m->name && c->vtable[k]->desc == m->desc)) k++;
            if (k == vsize) vsize++;
            c->vtable[k] = m;
            m->vtable_index = (int)k;
        }
    c->vtable_size = vsize;
    if (s && (s->flags & CF_THROWABLE)) c->flags |= CF_THROWABLE;
    if (!strcmp(c->name, "java/lang/Throwable")) c->flags |= CF_THROWABLE;
    c->state = CS_LINKED;
}

static Class *make_array_class(Thread *t, const char *name)
{
    Class *component;
    if (name[1] == '[') component = load_class(t, name + 1);
    else if (name[1] == 'L') {
        uint32_t n = (uint32_t)strlen(name);
        char inner[512];
        if (n < 4 || n - 3 >= sizeof(inner) || name[n - 1] != ';') { throw_new(t, "java/lang/NoClassDefFoundError", name); return 0; }
        memcpy(inner, name + 2, n - 3); inner[n - 3] = 0;
        component = load_class(t, inner);
    } else {
        static const char codes[] = "ZCFDBSIJ";
        const char *p = name[2] ? 0 : strchr(codes, name[1]);
        if (!p || !name[1]) { throw_new(t, "java/lang/NoClassDefFoundError", name); return 0; }
        component = vm.prims[T_BOOLEAN + (p - codes)];
    }
    if (!component) return 0;
    if (component->array_class) return component->array_class;
    Class *c = meta_alloc(sizeof(Class));
    c->name = sym(name);
    c->super = vm.object;
    c->access = ACC_PUBLIC | ACC_FINAL | ACC_ABSTRACT;
    c->flags = CF_ARRAY;
    c->component = component;
    if (component->flags & CF_PRIMITIVE) {
        c->elem_type = component->elem_type;
        c->elem_size = component->elem_size;
    } else { c->elem_type = T_REF; c->elem_size = sizeof(Object *); c->flags |= CF_REFERENCE_ARRAY; }
    c->interface_count = 2;
    c->interfaces = meta_alloc(2 * sizeof(Class *));
    c->interfaces[0] = vm.cloneable; c->interfaces[1] = vm.serializable;
    c->all_interfaces = c->interfaces; c->all_interface_count = 2;
    c->vtable = vm.object->vtable; c->vtable_size = vm.object->vtable_size;
    c->instance_size = sizeof(Array);
    c->state = CS_READY;
    add_class(c);
    component->array_class = c;
    return c;
}

Class *load_class(Thread *t, const char *name)
{
    Class *c = find_loaded(name);
    if (c) {
        if (c->state == CS_LOADED) { throw_new(t, "java/lang/ClassCircularityError", name); return 0; }
        return c;
    }
    if (name[0] == '[') return vm.object ? make_array_class(t, name) : 0;
    char file[512];
    snprintf(file, sizeof(file), "%s.class", name);
    uint32_t size;
    uint8_t *bytes = resource_read(file, &size);
    if (!bytes) {
        char dotted[512];
        uint32_t i = 0;
        for (; name[i] && i + 1 < sizeof(dotted); i++) dotted[i] = name[i] == '/' ? '.' : name[i];
        dotted[i] = 0;
        if (!vm.throwable) fatal("missing core class", name);
        throw_new(t, "java/lang/NoClassDefFoundError", dotted);
        return 0;
    }
    if (vm_verbose > 1) vm_printf(1, "[load %s]\n", name);
    /* Register early as "loading" to detect circularity. */
    Class placeholder;
    memset(&placeholder, 0, sizeof(placeholder));
    placeholder.name = sym(name);
    placeholder.state = CS_LOADED;
    add_class(&placeholder);
    c = parse_class(t, bytes, size, placeholder.name);
    vm_free(bytes);
    /* Unhook the placeholder. */
    uint32_t h = hash_bytes(name, (uint32_t)strlen(name)) % CLASS_BUCKETS;
    for (Class **link = &classes[h]; *link; link = &(*link)->hash_next)
        if (*link == &placeholder) { *link = placeholder.hash_next; break; }
    if (!c) return 0;
    link_class(c);
    add_class(c);
    if (vm.object && c->super == 0 && strcmp(name, "java/lang/Object")) c->super = vm.object;
    for (uint16_t i = 0; i < c->method_count; i++)
        if (!strcmp(c->methods[i].name, "finalize") && !strcmp(c->methods[i].desc, "()V") && strcmp(name, "java/lang/Object"))
            c->finalizer = &c->methods[i];
    return c;
}

Class *array_of(Thread *t, Class *e)
{
    if (e->array_class) return e->array_class;
    char name[512];
    if (e->flags & CF_PRIMITIVE) snprintf(name, sizeof(name), "[%c", "????ZCFDBSIJ"[e->elem_type]);
    else if (e->flags & CF_ARRAY) snprintf(name, sizeof(name), "[%s", e->name);
    else snprintf(name, sizeof(name), "[L%s;", e->name);
    return load_class(t, name);
}

Class *class_for_descriptor(Thread *t, const char *desc)
{
    switch (desc[0]) {
    case 'Z': return vm.prims[T_BOOLEAN]; case 'C': return vm.prims[T_CHAR]; case 'F': return vm.prims[T_FLOAT];
    case 'D': return vm.prims[T_DOUBLE]; case 'B': return vm.prims[T_BYTE]; case 'S': return vm.prims[T_SHORT];
    case 'I': return vm.prims[T_INT]; case 'J': return vm.prims[T_LONG]; case 'V': return vm.prims[0];
    case '[': {
        const char *p = desc;
        while (*p == '[') p++;
        uint32_t n = (uint32_t)(p - desc) + (*p == 'L' ? (uint32_t)(strchr(p, ';') - p) + 1 : 1);
        char name[512];
        if (n >= sizeof(name)) return 0;
        memcpy(name, desc, n); name[n] = 0;
        return load_class(t, name);
    }
    case 'L': {
        const char *end = strchr(desc, ';');
        char name[512];
        uint32_t n = (uint32_t)(end - desc - 1);
        if (!end || n >= sizeof(name)) return 0;
        memcpy(name, desc + 1, n); name[n] = 0;
        return load_class(t, name);
    }
    }
    return 0;
}

/* ---- lookup ---- */
Method *find_declared_method(Class *c, const char *name, const char *desc)
{
    for (uint16_t i = 0; i < c->method_count; i++)
        if (!strcmp(c->methods[i].name, name) && (!desc || !strcmp(c->methods[i].desc, desc))) return &c->methods[i];
    return 0;
}
Method *find_method(Class *c, const char *name, const char *desc)
{
    for (Class *k = c; k; k = k->super) {
        Method *m = find_declared_method(k, name, desc);
        if (m) return m;
    }
    Method *abstract = 0;
    for (uint16_t i = 0; i < c->all_interface_count; i++) {
        Method *m = find_declared_method(c->all_interfaces[i], name, desc);
        if (m && !(m->access & ACC_ABSTRACT)) return m;
        if (m && !abstract) abstract = m;
    }
    return abstract;
}
Field *find_field(Class *c, const char *name, const char *desc)
{
    for (Class *k = c; k; k = k->super) {
        for (uint16_t i = 0; i < k->field_count; i++)
            if (!strcmp(k->fields[i].name, name) && (!desc || !strcmp(k->fields[i].desc, desc))) return &k->fields[i];
        for (uint16_t i = 0; i < k->all_interface_count; i++) {
            Class *f = k->all_interfaces[i];
            for (uint16_t j = 0; j < f->field_count; j++)
                if (!strcmp(f->fields[j].name, name) && (!desc || !strcmp(f->fields[j].desc, desc))) return &f->fields[j];
        }
    }
    return 0;
}
uint32_t field_offset(Class *c, const char *name)
{
    Field *f = find_field(c, name, 0);
    if (!f) fatal("missing core field", name);
    return f->offset;
}

Method *find_virtual(Class *c, Method *m)
{
    const char *name = m->name, *desc = m->desc;
    for (uint32_t i = c->vtable_size; i-- > 0;)
        if (c->vtable[i]->name == name && c->vtable[i]->desc == desc) return c->vtable[i];
    for (uint16_t i = 0; i < c->all_interface_count; i++) {
        Method *d = find_declared_method(c->all_interfaces[i], name, desc);
        if (d && !(d->access & ACC_ABSTRACT)) return d;
    }
    return 0;
}

int is_assignable(Class *from, Class *to)
{
    if (from == to || to == vm.object) return to != vm.object || !(from->flags & CF_PRIMITIVE);
    if (from->cast_cache == to) return 1;
    int ok = 0;
    if (from->flags & CF_ARRAY) {
        if (to->flags & CF_ARRAY) {
            Class *a = from->component, *b = to->component;
            ok = (a->flags & CF_PRIMITIVE) || (b->flags & CF_PRIMITIVE) ? a == b : is_assignable(a, b);
        } else ok = to == vm.cloneable || to == vm.serializable;
    } else if (to->access & ACC_INTERFACE) {
        for (uint16_t i = 0; i < from->all_interface_count; i++) if (from->all_interfaces[i] == to) { ok = 1; break; }
    } else if (!(to->flags & CF_ARRAY)) {
        for (Class *k = from->super; k; k = k->super) if (k == to) { ok = 1; break; }
    }
    if (ok) from->cast_cache = to;
    return ok;
}
int instance_of(Object *o, Class *c) { return o && is_assignable(o->cls, c); }

/* ---- initialization ---- */
static void set_constants(Thread *t, Class *c)
{
    ConstPool *cp = &c->cp;
    for (uint16_t i = 0; i < c->field_count; i++) {
        Field *f = &c->fields[i];
        if (!(f->access & ACC_STATIC) || !f->constant || f->constant >= cp->count) continue;
        uint8_t *p = c->statics + f->offset;
        uint16_t k = f->constant;
        switch (cp->tags[k]) {
        case CP_INT:
            switch (f->kind) {
            case K_BYTE: case K_BOOL: *(int8_t *)p = (int8_t)cp->data[k]; break;
            case K_CHAR: case K_SHORT: *(uint16_t *)p = (uint16_t)cp->data[k]; break;
            default: *(uint32_t *)p = cp->data[k];
            }
            break;
        case CP_FLOAT: *(uint32_t *)p = cp->data[k]; break;
        case CP_LONG: case CP_DOUBLE: {
            uint64_t v = (uint64_t)cp->data[k] << 32 | cp->data[k + 1];
            memcpy(p, &v, 8);
            break;
        }
        case CP_STRING: *(Object **)p = intern_utf8(t, cp_utf8(cp, (uint16_t)cp->data[k])); break;
        }
    }
}

int init_class(Thread *t, Class *c)
{
    for (;;) {
        if (c->state == CS_READY) return 0;
        if (c->state == CS_ERROR) {
            char name[256];
            throw_new(t, "java/lang/NoClassDefFoundError", class_java_name(c, name, sizeof(name)));
            return -1;
        }
        if (c->state != CS_INITIALIZING) break;
        if (c->init_thread == t) return 0;
        thread_yield(t);
    }
    c->state = CS_INITIALIZING;
    c->init_thread = t;
    if (c->super && init_class(t, c->super)) { c->state = CS_ERROR; return -1; }
    set_constants(t, c);
    Method *clinit = find_declared_method(c, "<clinit>", "()V");
    if (clinit && invoke(t, clinit, 0, 0)) {
        Object *e = t->exception;
        Class *error = load_class(t, "java/lang/Error");
        if (error && !instance_of(e, error)) {
            t->exception = 0;
            Class *eiie = load_class(t, "java/lang/ExceptionInInitializerError");
            Method *ctor = eiie ? find_declared_method(eiie, "<init>", "(Ljava/lang/Throwable;)V") : 0;
            Object *wrapper = eiie && init_class(t, eiie) == 0 ? new_object(t, eiie) : 0;
            if (wrapper && ctor) {
                Slot args[2]; args[0].l = wrapper; args[1].l = e;
                if (!invoke(t, ctor, args, 0)) t->exception = wrapper;
            } else t->exception = e;
        }
        c->state = CS_ERROR;
        return -1;
    }
    c->state = CS_READY;
    c->init_thread = 0;
    return 0;
}

/* ---- resolution ---- */
Class *resolve_class(Thread *t, Class *from, uint16_t index)
{
    ConstPool *cp = &from->cp;
    if (cp->resolved[index]) return cp->resolved[index];
    const char *name = cp_class_name(cp, index);
    if (!name) { throw_new(t, "java/lang/IncompatibleClassChangeError", "bad class reference"); return 0; }
    Class *c = load_class(t, name);
    if (c) cp->resolved[index] = c;
    return c;
}

static void member_error(Thread *t, const char *kind, Class *c, const char *name, const char *desc)
{
    char text[600], cname[256];
    snprintf(text, sizeof(text), "%s.%s%s%s", class_java_name(c, cname, sizeof(cname)), name, desc[0] == '(' ? "" : ":", desc);
    throw_new(t, kind, text);
}

Field *resolve_field(Thread *t, Class *from, uint16_t index, int is_static)
{
    ConstPool *cp = &from->cp;
    if (cp->resolved[index]) return cp->resolved[index];
    uint32_t d = cp->data[index];
    Class *c = resolve_class(t, from, (uint16_t)(d >> 16));
    if (!c) return 0;
    uint32_t nt = cp->data[d & 0xffff];
    const char *name = cp_utf8(cp, (uint16_t)(nt >> 16)), *desc = cp_utf8(cp, (uint16_t)nt);
    Field *f = find_field(c, name, desc);
    if (!f) { member_error(t, "java/lang/NoSuchFieldError", c, name, desc); return 0; }
    if (!!(f->access & ACC_STATIC) != is_static) { member_error(t, "java/lang/IncompatibleClassChangeError", c, name, desc); return 0; }
    cp->resolved[index] = f;
    return f;
}

Method *resolve_method(Thread *t, Class *from, uint16_t index)
{
    ConstPool *cp = &from->cp;
    if (cp->resolved[index]) return cp->resolved[index];
    uint32_t d = cp->data[index];
    Class *c = resolve_class(t, from, (uint16_t)(d >> 16));
    if (!c) return 0;
    uint32_t nt = cp->data[d & 0xffff];
    const char *name = cp_utf8(cp, (uint16_t)(nt >> 16)), *desc = cp_utf8(cp, (uint16_t)nt);
    Method *m = find_method(c, name, desc);
    if (!m && (c->access & ACC_INTERFACE)) m = find_method(vm.object, name, desc);
    if (!m) { member_error(t, "java/lang/NoSuchMethodError", c, name, desc); return 0; }
    cp->resolved[index] = m;
    return m;
}

Object *resolve_string(Thread *t, Class *from, uint16_t index)
{
    ConstPool *cp = &from->cp;
    if (cp->resolved[index]) return cp->resolved[index];
    Object *s = intern_utf8(t, cp_utf8(cp, (uint16_t)cp->data[index]));
    if (s) cp->resolved[index] = s;
    return s;
}

/* ---- mirrors ---- */
Object *class_mirror(Thread *t, Class *c)
{
    if (c->mirror) return c->mirror;
    Object *m = new_object(t, vm.class_);
    if (!m) return 0;
    FIELD(m, vm.class_vm, Class *) = c;
    c->mirror = m;
    return m;
}
Class *mirror_class(Object *mirror) { return mirror ? FIELD(mirror, vm.class_vm, Class *) : 0; }
