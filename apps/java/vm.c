/* VM core: start-up, strings and interning, exceptions and stack traces,
 * console output, metadata memory. */
#include "jvm.h"
#include <stdarg.h>
#include <stdio.h>

VM vm;
int vm_verbose;
const char *vm_trace_class, *vm_trace_method;
int method_line(Method *m, int pc);

void vm_trace_call(Thread *t, Method *m)
{
    if (strcmp(m->name, vm_trace_method) || strcmp(m->owner->name, vm_trace_class)) return;
    char name[256];
    vm_printf(1, "bjvm: call %s.%s%s\n", class_java_name(m->owner, name, sizeof(name)), m->name, m->desc);
    for (Frame *f = t->fp; f > t->frames; f--) {
        if (!f->method || !f->method->owner) continue;
        vm_printf(1, "\tat %s.%s (line %d)\n", class_java_name(f->method->owner, name, sizeof(name)), f->method->name,
                  method_line(f->method, f->method->code ? (int)(f->pc - f->method->code) : -1));
    }
}
int vm_exit_requested, vm_exit_status;
char vm_home[256];

void vm_print(int err, const char *text) { plat_write(err, text, (uint32_t)strlen(text)); }
void vm_printf(int err, const char *format, ...)
{
    char text[1024];
    va_list ap;
    va_start(ap, format);
    vsnprintf(text, sizeof(text), format, ap);
    va_end(ap);
    vm_print(err, text);
}
void fatal(const char *message, const char *detail)
{
    vm_printf(1, "bjvm: fatal: %s%s%s\n", message, detail ? ": " : "", detail ? detail : "");
    vm_exit(134);
}

/* Metadata: bump allocation in 256 KiB chunks. */
static uint8_t *meta_at, *meta_end;
void *meta_alloc(uint32_t bytes)
{
    bytes = (bytes + 7) & ~7U;
    if (!bytes) bytes = 8;
    if (meta_at + bytes > meta_end) {
        uint32_t chunk = bytes > (256U << 10) ? bytes : 256U << 10;
        meta_at = vm_malloc(chunk);
        if (!meta_at) fatal("out of memory", "class metadata");
        meta_end = meta_at + chunk;
        memset(meta_at, 0, chunk);
    }
    void *p = meta_at;
    meta_at += bytes;
    return p;
}
char *meta_strdup(const char *s)
{
    uint32_t n = (uint32_t)strlen(s) + 1;
    char *p = meta_alloc(n);
    memcpy(p, s, n);
    return p;
}

/* ---- strings ---- */
jint string_length(Object *s) { Array *v = FIELD(s, vm.string_value, Array *); return v ? v->length : 0; }
jchar *string_chars(Object *s) { Array *v = FIELD(s, vm.string_value, Array *); return v ? ADATA(v, jchar) : 0; }

Object *new_string_chars(Thread *t, const jchar *chars, jint length)
{
    Array *v = new_prim_array(t, T_CHAR, length);
    if (!v) return 0;
    if (length) memcpy(ADATA(v, jchar), chars, (size_t)length * 2);
    Object *s = new_object(t, vm.string);
    if (!s) return 0;
    FIELD(s, vm.string_value, Array *) = v;
    return s;
}

static jint utf8_decode(const char *p, jchar *out)
{
    jint n = 0;
    const uint8_t *s = (const uint8_t *)p;
    while (*s) {
        uint32_t c = *s++;
        if (c >= 0xf0 && (s[0] & 0xc0) == 0x80 && (s[1] & 0xc0) == 0x80 && (s[2] & 0xc0) == 0x80) {
            c = (c & 7) << 18 | (s[0] & 0x3fU) << 12 | (s[1] & 0x3fU) << 6 | (s[2] & 0x3fU);
            s += 3;
            if (out) { c -= 0x10000; out[n] = (jchar)(0xd800 + (c >> 10)); out[n + 1] = (jchar)(0xdc00 + (c & 0x3ff)); }
            n += 2;
            continue;
        }
        if (c >= 0xe0 && (s[0] & 0xc0) == 0x80 && (s[1] & 0xc0) == 0x80) { c = (c & 15) << 12 | (s[0] & 0x3fU) << 6 | (s[1] & 0x3fU); s += 2; }
        else if (c >= 0xc0 && (s[0] & 0xc0) == 0x80) { c = (c & 31) << 6 | (s[0] & 0x3fU); s++; }
        if (out) out[n] = (jchar)c;
        n++;
    }
    return n;
}

Object *new_string(Thread *t, const char *utf8)
{
    jint n = utf8_decode(utf8, 0);
    Array *v = new_prim_array(t, T_CHAR, n);
    if (!v) return 0;
    utf8_decode(utf8, ADATA(v, jchar));
    Object *s = new_object(t, vm.string);
    if (!s) return 0;
    FIELD(s, vm.string_value, Array *) = v;
    return s;
}

char *string_utf8(Object *s, char *buffer, uint32_t size)
{
    if (!s) return 0;
    jint n = string_length(s);
    jchar *c = string_chars(s);
    uint32_t at = 0;
    for (jint i = 0; i < n; i++) {
        uint32_t u = c[i];
        if (u >= 0xd800 && u < 0xdc00 && i + 1 < n && c[i + 1] >= 0xdc00 && c[i + 1] < 0xe000) {
            u = 0x10000 + ((u - 0xd800) << 10) + (c[i + 1] - 0xdc00);
            i++;
        }
        char tmp[4];
        uint32_t k;
        if (u < 0x80) { tmp[0] = (char)u; k = 1; }
        else if (u < 0x800) { tmp[0] = (char)(0xc0 | u >> 6); tmp[1] = (char)(0x80 | (u & 63)); k = 2; }
        else if (u < 0x10000) { tmp[0] = (char)(0xe0 | u >> 12); tmp[1] = (char)(0x80 | (u >> 6 & 63)); tmp[2] = (char)(0x80 | (u & 63)); k = 3; }
        else { tmp[0] = (char)(0xf0 | u >> 18); tmp[1] = (char)(0x80 | (u >> 12 & 63)); tmp[2] = (char)(0x80 | (u >> 6 & 63)); tmp[3] = (char)(0x80 | (u & 63)); k = 4; }
        if (at + k + 1 > size) break;
        memcpy(buffer + at, tmp, k);
        at += k;
    }
    if (size) buffer[at] = 0;
    return buffer;
}

/* Interned strings: open hash table of String objects. */
static Object **interned;
static uint32_t intern_capacity, intern_count;
static uint32_t chars_hash(const jchar *c, jint n) { uint32_t h = 0; for (jint i = 0; i < n; i++) h = 31 * h + c[i]; return h; }

static Object *intern_lookup(const jchar *c, jint n, uint32_t *slot)
{
    uint32_t i = chars_hash(c, n) & (intern_capacity - 1);
    for (;; i = (i + 1) & (intern_capacity - 1)) {
        Object *s = interned[i];
        if (!s) { *slot = i; return 0; }
        if (string_length(s) == n && !memcmp(string_chars(s), c, (size_t)n * 2)) return s;
    }
}
static void intern_grow(void)
{
    uint32_t old = intern_capacity;
    Object **table = interned;
    intern_capacity = old ? old * 2 : 4096;
    interned = vm_malloc(intern_capacity * sizeof(Object *));
    if (!interned) fatal("out of memory", "string table");
    memset(interned, 0, intern_capacity * sizeof(Object *));
    for (uint32_t i = 0; i < old; i++)
        if (table[i]) { uint32_t slot; intern_lookup(string_chars(table[i]), string_length(table[i]), &slot); interned[slot] = table[i]; }
    vm_free(table);
}
Object *intern_string(Thread *t, Object *s)
{
    (void)t;
    if (!s) return 0;
    if ((intern_count + 1) * 2 > intern_capacity) intern_grow();
    uint32_t slot;
    Object *found = intern_lookup(string_chars(s), string_length(s), &slot);
    if (found) return found;
    interned[slot] = s;
    intern_count++;
    return s;
}
Object *intern_utf8(Thread *t, const char *utf8)
{
    jchar small[256];
    jint n = utf8_decode(utf8, 0);
    if (n <= 256 && intern_capacity) {
        utf8_decode(utf8, small);
        uint32_t slot;
        Object *found = intern_lookup(small, n, &slot);
        if (found) return found;
    }
    Object *s = new_string(t, utf8);
    return s ? intern_string(t, s) : 0;
}
void strings_scan_roots(void (*mark_one)(Object *o))
{
    for (uint32_t i = 0; i < intern_capacity; i++) if (interned[i]) mark_one(interned[i]);
}

/* ---- exceptions ---- */
void throw_object(Thread *t, Object *e) { t->exception = e; }

void throw_new(Thread *t, const char *class_name, const char *message)
{
    Object *saved = t->exception;
    t->exception = 0;
    Class *c = load_class(t, class_name);
    if (!c) {
        vm_printf(1, "bjvm: cannot create %s: %s\n", class_name, message ? message : "");
        fatal("exception class unavailable", class_name);
    }
    if (init_class(t, c)) return;
    Object *e = new_object(t, c);
    if (!e) return;
    Object *text = message ? new_string(t, message) : 0;
    Method *ctor = find_declared_method(c, "<init>", "(Ljava/lang/String;)V");
    Slot args[2];
    args[0].l = e; args[1].l = text;
    if (ctor) invoke(t, ctor, args, 0);
    else {
        ctor = find_declared_method(c, "<init>", "()V");
        if (ctor) invoke(t, ctor, args, 0);
        if (text) FIELD(e, vm.throwable_message, Object *) = text;
    }
    (void)saved;
    if (!t->exception) t->exception = e;
}

void throw_npe(Thread *t) { throw_new(t, "java/lang/NullPointerException", 0); }

/* A backtrace: int[] of (method index, pc) pairs, plus a parallel array of
 * Method pointers stored as ints (32-bit VM). */
Object *make_trace(Thread *t, int skip)
{
    int n = 0;
    for (Frame *f = t->fp; f > t->frames; f--) if (f->method && f->method->owner) n++;
    Array *a = new_prim_array(t, T_INT, n * 2);
    if (!a) return 0;
    int i = 0;
    for (Frame *f = t->fp; f > t->frames; f--) {
        Method *m = f->method;
        if (!m || !m->owner) continue;
        if (skip > 0) { skip--; continue; }
        ADATA(a, jint)[i * 2] = (jint)(uintptr_t)m;
        ADATA(a, jint)[i * 2 + 1] = m->code ? (jint)(f->pc - m->code) : -1;
        i++;
    }
    a->length = i * 2;
    return &a->o;
}

int method_line(Method *m, int pc)
{
    int line = -1;
    if (pc < 0 || !m->lines) return m->access & ACC_NATIVE ? -2 : -1;
    for (uint16_t i = 0; i < m->line_count; i++) if (m->lines[i].pc <= pc) line = m->lines[i].line;
    return line;
}

static void print_trace_lines(Object *e)
{
    Array *trace = FIELD(e, vm.throwable_trace, Array *);
    if (!trace || trace->o.cls != vm.prim_arrays[T_INT]) return;
    for (jint i = 0; i + 1 < trace->length; i += 2) {
        Method *m = (Method *)(uintptr_t)ADATA(trace, jint)[i];
        char name[256];
        int line = method_line(m, ADATA(trace, jint)[i + 1]);
        if (line >= 0) vm_printf(1, "\tat %s.%s(%s:%d)\n", class_java_name(m->owner, name, sizeof(name)), m->name, m->owner->source_file ? m->owner->source_file : "Unknown Source", line);
        else vm_printf(1, "\tat %s.%s(%s)\n", class_java_name(m->owner, name, sizeof(name)), m->name, line == -2 ? "Native Method" : m->owner->source_file ? m->owner->source_file : "Unknown Source");
    }
}

void print_exception(Thread *t, Object *e)
{
    /* Prefer Java's printStackTrace (causes, overridden toString). */
    Method *p = find_method(e->cls, "printStackTrace", "()V");
    Slot args[1];
    args[0].l = e;
    if (p && !invoke(t, p, args, 0)) return;
    t->exception = 0;
    char name[256], text[512];
    Object *message = FIELD(e, vm.throwable_message, Object *);
    vm_printf(1, "%s%s%s\n", class_java_name(e->cls, name, sizeof(name)), message ? ": " : "", message ? string_utf8(message, text, sizeof(text)) : "");
    print_trace_lines(e);
}

void vm_uncaught(Thread *t, Object *e)
{
    Class *death = find_loaded("java/lang/ThreadDeath");
    if (death && instance_of(e, death)) return;
    char name[256];
    Object *tn = t->jthread ? FIELD(t->jthread, vm.thread_name, Object *) : 0;
    vm_printf(1, "Exception in thread \"%s\" ", tn ? string_utf8(tn, name, sizeof(name)) : "?");
    print_exception(t, e);
    if (t->id == 1) vm_exit_status = 1;
}

/* ---- start-up ---- */
static Class *core(Thread *t, const char *name)
{
    Class *c = load_class(t, name);
    if (!c) {
        if (t->exception) print_exception(t, t->exception);
        fatal("cannot load core class", name);
    }
    return c;
}

static Class *primitive(const char *name, int type, int size)
{
    Class *c = meta_alloc(sizeof(Class));
    c->name = symbol(name, (uint32_t)strlen(name));
    c->flags = CF_PRIMITIVE;
    c->access = ACC_PUBLIC | ACC_FINAL | ACC_ABSTRACT;
    c->elem_type = (uint8_t)type;
    c->elem_size = (uint8_t)size;
    c->state = CS_READY;
    return c;
}

extern int bjvm_main_status;
static Thread *boot_thread;

static int start_vm(Thread *t, const char *main_class, int argc, const char *const *argv)
{
    vm.prims[0] = primitive("void", 0, 0);
    vm.prims[T_BOOLEAN] = primitive("boolean", T_BOOLEAN, 1);
    vm.prims[T_CHAR] = primitive("char", T_CHAR, 2);
    vm.prims[T_FLOAT] = primitive("float", T_FLOAT, 4);
    vm.prims[T_DOUBLE] = primitive("double", T_DOUBLE, 8);
    vm.prims[T_BYTE] = primitive("byte", T_BYTE, 1);
    vm.prims[T_SHORT] = primitive("short", T_SHORT, 2);
    vm.prims[T_INT] = primitive("int", T_INT, 4);
    vm.prims[T_LONG] = primitive("long", T_LONG, 8);
    vm.object = core(t, "java/lang/Object");
    vm.cloneable = core(t, "java/lang/Cloneable");
    vm.serializable = core(t, "java/io/Serializable");
    vm.class_ = core(t, "java/lang/Class");
    vm.class_vm = field_offset(vm.class_, "vmClass");
    vm.string = core(t, "java/lang/String");
    vm.string_value = field_offset(vm.string, "value");
    vm.string_hash = field_offset(vm.string, "hash");
    vm.throwable = core(t, "java/lang/Throwable");
    vm.throwable_trace = field_offset(vm.throwable, "backtrace");
    vm.throwable_message = field_offset(vm.throwable, "detailMessage");
    for (int i = T_BOOLEAN; i <= T_LONG; i++) vm.prim_arrays[i] = array_of(t, vm.prims[i]);
    vm.thread = core(t, "java/lang/Thread");
    vm.thread_vm = field_offset(vm.thread, "vmThread");
    vm.thread_name = field_offset(vm.thread, "name");
    vm.thread_daemon = field_offset(vm.thread, "daemon");
    vm.thread_priority = field_offset(vm.thread, "priority");
    vm.thread_alive = field_offset(vm.thread, "alive");
    vm.thread_run = find_declared_method(vm.thread, "run", "()V");
    vm.system = core(t, "java/lang/System");
    vm.error = core(t, "java/lang/Error");
    vm.stack_trace_element = core(t, "java/lang/StackTraceElement");
    vm.string_array = array_of(t, vm.string);
    vm.object_array = array_of(t, vm.object);
    vm.class_array = array_of(t, vm.class_);
    Class *buffer = core(t, "java/nio/Buffer");
    vm.buffer_position = field_offset(buffer, "position");
    vm.buffer_limit = field_offset(buffer, "limit");
    vm.buffer_capacity = field_offset(buffer, "capacity");
    vm.buffer_array = field_offset(buffer, "hb");
    vm.buffer_offset = field_offset(buffer, "offset");
    natives_init();

    /* The main thread object, then System's initialization. */
    if (init_class(t, vm.thread)) goto failed;
    Object *jt = new_object(t, vm.thread);
    if (!jt) goto failed;
    t->jthread = jt;
    FIELD(jt, vm.thread_vm, Thread *) = t;
    FIELD(jt, vm.thread_alive, jboolean) = 1;
    FIELD(jt, vm.thread_priority, jint) = 5;
    FIELD(jt, vm.thread_name, Object *) = new_string(t, "main");
    if (init_class(t, vm.system)) goto failed;
    Method *init = find_declared_method(vm.system, "initialize", "()V");
    if (init && invoke(t, init, 0, 0)) goto failed;

    char name[512];
    uint32_t i = 0;
    for (; main_class[i] && i + 1 < sizeof(name); i++) name[i] = main_class[i] == '.' ? '/' : main_class[i];
    name[i] = 0;
    Class *c = load_class(t, name);
    if (!c) {
        t->exception = 0;
        vm_printf(1, "Error: Could not find or load main class %s\n", main_class);
        return 1;
    }
    Method *m = find_declared_method(c, "main", "([Ljava/lang/String;)V");
    if (!m || !(m->access & ACC_STATIC)) {
        vm_printf(1, "Error: Main method not found in class %s, please define the main method as:\n   public static void main(String[] args)\n", main_class);
        return 1;
    }
    Array *args = new_array(t, vm.string_array, argc);
    if (!args) goto failed;
    for (int k = 0; k < argc; k++) {
        Object *s = new_string(t, argv[k]);
        if (!s) goto failed;
        ADATA(args, Object *)[k] = s;
    }
    if (init_class(t, c)) goto failed;
    Slot a[1];
    a[0].l = &args->o;
    if (invoke(t, m, a, 0)) goto failed;
    return 0;
failed:
    if (t->exception) {
        Object *e = t->exception;
        t->exception = 0;
        vm_uncaught(t, e);
    }
    return 1;
}

static const char *boot_main;
static int boot_argc;
static const char *const *boot_argv;

static void main_thread_body(Thread *t)
{
    int status = start_vm(t, boot_main, boot_argc, boot_argv);
    if (status) vm_exit_status = status;
}

/* Entry from the launcher: runs the program until the last non-daemon thread
 * ends or System.exit. */
extern void bjvm_set_main_body(void (*body)(Thread *t));
int vm_run(const char *main_class, int argc, const char *const *argv)
{
    boot_main = main_class; boot_argc = argc; boot_argv = argv;
    boot_thread = thread_create(0, 1);
    if (!boot_thread) { vm_print(1, "bjvm: not enough memory for the main thread\n"); return 1; }
    boot_thread->start_method = 0;
    bjvm_set_main_body(main_thread_body);
    threads_run_until_done(boot_thread);
    return vm_exit_status;
}
