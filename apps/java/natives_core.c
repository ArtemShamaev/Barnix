/* Natives of java.lang and java.lang.reflect. */
#include "jvm.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define N(name) static void name(Thread *t, Slot *a)
#define UNUSED (void)t; (void)a

/* ---- java.lang.Object ---- */
N(object_get_class) { a[0].l = class_mirror(t, a[0].l->cls); }
N(object_hash) { (void)t; a[0].i = (jint)((uintptr_t)a[0].l >> 3) * (jint)0x9e3779b1U; }
N(object_clone)
{
    Object *o = a[0].l;
    if (!(o->cls->flags & CF_ARRAY) && !is_assignable(o->cls, vm.cloneable)) {
        char name[256];
        throw_new(t, "java/lang/CloneNotSupportedException", class_java_name(o->cls, name, sizeof(name)));
        return;
    }
    a[0].l = clone_object(t, o);
}
N(object_notify) { monitor_notify(t, a[0].l, 0); }
N(object_notify_all) { monitor_notify(t, a[0].l, 1); }
N(object_wait)
{
    jlong ms = get_long(a + 1);
    if (ms < 0) { throw_new(t, "java/lang/IllegalArgumentException", "timeout value is negative"); return; }
    monitor_wait(t, a[0].l, ms);
}

/* ---- java.lang.Class ---- */
static Class *self_class(Slot *a) { return mirror_class(a[0].l); }
N(class_name)
{
    Class *c = self_class(a);
    char name[512];
    a[0].l = new_string(t, class_java_name(c, name, sizeof(name)));
}
N(class_for_name)
{
    char name[512], internal[512];
    if (!a[0].l) { throw_npe(t); return; }
    string_utf8(a[0].l, name, sizeof(name));
    uint32_t i = 0;
    for (; name[i]; i++) internal[i] = name[i] == '.' ? '/' : name[i];
    internal[i] = 0;
    if (strchr(name, '/')) { throw_new(t, "java/lang/ClassNotFoundException", name); return; }
    Class *c = load_class(t, internal);
    if (!c) {
        t->exception = 0;
        throw_new(t, "java/lang/ClassNotFoundException", name);
        return;
    }
    if (a[1].i && init_class(t, c)) return;
    a[0].l = class_mirror(t, c);
}
N(class_is_instance) { (void)t; a[0].i = instance_of(a[1].l, self_class(a)); }
N(class_is_assignable)
{
    if (!a[1].l) { throw_npe(t); return; }
    Class *to = self_class(a), *from = mirror_class(a[1].l);
    a[0].i = (from->flags & CF_PRIMITIVE) || (to->flags & CF_PRIMITIVE) ? from == to : is_assignable(from, to);
}
N(class_is_interface) { (void)t; a[0].i = !!(self_class(a)->access & ACC_INTERFACE); }
N(class_is_array) { (void)t; a[0].i = !!(self_class(a)->flags & CF_ARRAY); }
N(class_is_primitive) { (void)t; a[0].i = !!(self_class(a)->flags & CF_PRIMITIVE); }
N(class_modifiers) { (void)t; a[0].i = self_class(a)->access & 0x7631; }
N(class_super)
{
    Class *c = self_class(a);
    a[0].l = c->super && !(c->access & ACC_INTERFACE) && !(c->flags & CF_PRIMITIVE) ? class_mirror(t, c->super) : 0;
}
N(class_component) { Class *c = self_class(a); a[0].l = c->component ? class_mirror(t, c->component) : 0; }
N(class_interfaces)
{
    Class *c = self_class(a);
    Array *r = new_array(t, vm.class_array, c->interface_count);
    if (!r) return;
    for (uint16_t i = 0; i < c->interface_count; i++) {
        Object *m = class_mirror(t, c->interfaces[i]);
        if (!m) return;
        ADATA(r, Object *)[i] = m;
    }
    a[0].l = &r->o;
}
N(class_primitive)
{
    char name[32];
    string_utf8(a[0].l, name, sizeof(name));
    for (int i = 0; i <= T_LONG; i++)
        if (vm.prims[i] && !strcmp(vm.prims[i]->name, name)) { a[0].l = class_mirror(t, vm.prims[i]); return; }
    a[0].l = 0;
}
N(class_init) { Class *c = self_class(a); init_class(t, c); }
N(class_resource_bytes)
{
    char name[512];
    if (!a[0].l) { a[0].l = 0; return; }
    string_utf8(a[0].l, name, sizeof(name));
    uint32_t size;
    uint8_t *data = resource_read(name, &size);
    if (!data) { a[0].l = 0; return; }
    Array *r = new_prim_array(t, T_BYTE, (jint)size);
    if (r) memcpy(ADATA(r, uint8_t), data, size);
    vm_free(data);
    a[0].l = r ? &r->o : 0;
}

/* Reflection: java.lang.reflect.Constructor/Method hold (class, slot). */
static Class *param_class(Thread *t, const char **p)
{
    const char *d = *p;
    const char *e = d;
    while (*e == '[') e++;
    if (*e == 'L') e = strchr(e, ';');
    e++;
    char desc[512];
    uint32_t n = (uint32_t)(e - d);
    if (n >= sizeof(desc)) n = sizeof(desc) - 1;
    memcpy(desc, d, n); desc[n] = 0;
    *p = e;
    return class_for_descriptor(t, desc);
}

static Array *param_types(Thread *t, Method *m, Class **ret)
{
    int n = 0;
    for (const char *p = m->desc + 1; *p != ')';) { const char *q = p; Class *c = param_class(t, &q); if (!c) return 0; p = q; n++; }
    Array *r = new_array(t, vm.class_array, n);
    if (!r) return 0;
    const char *p = m->desc + 1;
    for (int i = 0; i < n; i++) {
        Class *c = param_class(t, &p);
        Object *mirror = c ? class_mirror(t, c) : 0;
        if (!mirror) return 0;
        ADATA(r, Object *)[i] = mirror;
    }
    if (ret) { p++; *ret = param_class(t, &p); }
    return r;
}

static Object *make_member(Thread *t, Class *owner, uint32_t slot, int constructor)
{
    Method *m = &owner->methods[slot];
    Class *rc = load_class(t, constructor ? "java/lang/reflect/Constructor" : "java/lang/reflect/Method");
    if (!rc || init_class(t, rc)) return 0;
    Class *ret = 0;
    Array *params = param_types(t, m, &ret);
    if (!params) return 0;
    Object *o = new_object(t, rc);
    if (!o) return 0;
    FIELD(o, field_offset(rc, "clazz"), Object *) = class_mirror(t, owner);
    FIELD(o, field_offset(rc, "slot"), jint) = (jint)slot;
    FIELD(o, field_offset(rc, "modifiers"), jint) = m->access & 0xfff;
    FIELD(o, field_offset(rc, "parameterTypes"), Array *) = params;
    if (!constructor) {
        FIELD(o, field_offset(rc, "name"), Object *) = intern_utf8(t, m->name);
        FIELD(o, field_offset(rc, "returnType"), Object *) = ret ? class_mirror(t, ret) : 0;
    }
    return o;
}

N(class_members)
{
    Class *c = self_class(a);
    int constructors = a[1].i, public_only = a[2].i;
    uint32_t n = 0;
    for (uint16_t i = 0; i < c->method_count; i++) {
        Method *m = &c->methods[i];
        if ((m->name == symbol("<init>", 6)) != !!constructors || (m->name[0] == '<' && !constructors)) continue;
        if (public_only && !(m->access & ACC_PUBLIC)) continue;
        n++;
    }
    Class *rc = load_class(t, constructors ? "java/lang/reflect/Constructor" : "java/lang/reflect/Method");
    if (!rc) return;
    Array *r = new_object_array(t, rc, (jint)n);
    if (!r) return;
    a[0].l = &r->o;
    uint32_t k = 0;
    for (uint16_t i = 0; i < c->method_count; i++) {
        Method *m = &c->methods[i];
        if ((m->name == symbol("<init>", 6)) != !!constructors || (m->name[0] == '<' && !constructors)) continue;
        if (public_only && !(m->access & ACC_PUBLIC)) continue;
        Object *o = make_member(t, c, i, constructors);
        if (!o) return;
        ADATA(r, Object *)[k++] = o;
    }
}

/* Box/unbox for reflection. */
static int unbox(Thread *t, Object *o, char type, Slot *out)
{
    if (type == 'L' || type == '[') { out->l = o; return 1; }
    if (!o) { throw_new(t, "java/lang/IllegalArgumentException", "null primitive argument"); return -1; }
    const char *n = o->cls->name;
    uint32_t off = sizeof(Object);
    Field *f = find_field(o->cls, "value", 0);
    if (!f || strncmp(n, "java/lang/", 10)) { throw_new(t, "java/lang/IllegalArgumentException", "argument type mismatch"); return -1; }
    off = f->offset;
    char from = f->desc[0];
    jlong l = 0; jdouble d = 0;
    switch (from) {
    case 'Z': case 'B': l = FIELD(o, off, int8_t); d = (jdouble)l; break;
    case 'C': l = FIELD(o, off, uint16_t); d = (jdouble)l; break;
    case 'S': l = FIELD(o, off, int16_t); d = (jdouble)l; break;
    case 'I': l = FIELD(o, off, jint); d = (jdouble)l; break;
    case 'J': l = FIELD(o, off, jlong); d = (jdouble)l; break;
    case 'F': d = FIELD(o, off, jfloat); l = (jlong)d; break;
    case 'D': d = FIELD(o, off, jdouble); l = (jlong)d; break;
    }
    switch (type) {
    case 'J': put_long(out, l); return 2;
    case 'D': put_double(out, from == 'F' || from == 'D' ? d : (jdouble)l); return 2;
    case 'F': out->f = (jfloat)(from == 'F' || from == 'D' ? d : (jdouble)l); return 1;
    default: out->i = (jint)l; return 1;
    }
}

static Object *box(Thread *t, char type, Slot *v)
{
    const char *cn, *desc;
    switch (type) {
    case 'Z': cn = "java/lang/Boolean"; desc = "(Z)Ljava/lang/Boolean;"; break;
    case 'B': cn = "java/lang/Byte"; desc = "(B)Ljava/lang/Byte;"; break;
    case 'C': cn = "java/lang/Character"; desc = "(C)Ljava/lang/Character;"; break;
    case 'S': cn = "java/lang/Short"; desc = "(S)Ljava/lang/Short;"; break;
    case 'I': cn = "java/lang/Integer"; desc = "(I)Ljava/lang/Integer;"; break;
    case 'J': cn = "java/lang/Long"; desc = "(J)Ljava/lang/Long;"; break;
    case 'F': cn = "java/lang/Float"; desc = "(F)Ljava/lang/Float;"; break;
    case 'D': cn = "java/lang/Double"; desc = "(D)Ljava/lang/Double;"; break;
    default: return v->l;
    }
    Class *c = load_class(t, cn);
    Method *m = c ? find_declared_method(c, "valueOf", desc) : 0;
    Slot r[2];
    if (!m || init_class(t, c) || invoke(t, m, v, r)) return 0;
    return r[0].l;
}

static void call_member(Thread *t, Slot *a, int constructor)
{
    Object *self = a[0].l;
    Class *rc = self->cls;
    Class *owner = mirror_class(FIELD(self, field_offset(rc, "clazz"), Object *));
    Method *m = &owner->methods[FIELD(self, field_offset(rc, "slot"), jint)];
    Object *receiver = constructor ? 0 : a[1].l;
    Array *args = (Array *)(constructor ? a[1].l : a[2].l);
    if (init_class(t, owner)) return;
    if (constructor) {
        if (owner->access & ACC_ABSTRACT) { throw_new(t, "java/lang/InstantiationException", 0); return; }
        receiver = new_object(t, owner);
        if (!receiver) return;
    } else if (!(m->access & ACC_STATIC)) {
        if (!receiver) { throw_npe(t); return; }
        if (!instance_of(receiver, owner)) { throw_new(t, "java/lang/IllegalArgumentException", "object is not an instance of declaring class"); return; }
        Method *v = find_virtual(receiver->cls, m);
        if (v) m = v;
    }
    Slot slots[256];
    int n = 0;
    if (!(m->access & ACC_STATIC)) slots[n++].l = receiver;
    jint count = args ? args->length : 0, i = 0;
    for (const char *p = m->desc + 1; *p != ')'; i++) {
        if (i >= count || n > 250) { throw_new(t, "java/lang/IllegalArgumentException", "wrong number of arguments"); return; }
        int k = unbox(t, ADATA(args, Object *)[i], *p, slots + n);
        if (k < 0) return;
        n += k;
        while (*p == '[') p++;
        if (*p == 'L') p = strchr(p, ';');
        p++;
    }
    if (i != count) { throw_new(t, "java/lang/IllegalArgumentException", "wrong number of arguments"); return; }
    Slot result[2] = {{0}, {0}};
    if (invoke(t, m, slots, result)) {
        Object *e = t->exception;
        t->exception = 0;
        Class *ite = load_class(t, "java/lang/reflect/InvocationTargetException");
        Method *ctor = ite ? find_declared_method(ite, "<init>", "(Ljava/lang/Throwable;)V") : 0;
        Object *w = ite && !init_class(t, ite) ? new_object(t, ite) : 0;
        if (!w || !ctor) { t->exception = e; return; }
        Slot ca[2]; ca[0].l = w; ca[1].l = e;
        if (!invoke(t, ctor, ca, 0)) t->exception = w;
        return;
    }
    a[0].l = constructor ? receiver : m->ret == 'V' ? 0 : box(t, m->ret, result);
}
N(ctor_new) { call_member(t, a, 1); }
N(method_invoke) { call_member(t, a, 0); }

N(class_enum_constants)
{
    Class *c = self_class(a);
    char desc[512];
    snprintf(desc, sizeof(desc), "()[L%s;", c->name);
    Method *m = find_declared_method(c, "values", desc);
    a[0].l = 0;
    if (!m || init_class(t, c)) return;
    Slot r[1];
    if (!invoke(t, m, 0, r)) a[0].l = r[0].l;
}

/* ---- java.lang.System ---- */
N(system_arraycopy)
{
    Array *src = (Array *)a[0].l, *dst = (Array *)a[2].l;
    jint sp = a[1].i, dp = a[3].i, n = a[4].i;
    if (!src || !dst) { throw_npe(t); return; }
    Class *sc = src->o.cls, *dc = dst->o.cls;
    if (!(sc->flags & CF_ARRAY) || !(dc->flags & CF_ARRAY)) { throw_new(t, "java/lang/ArrayStoreException", "arraycopy: not an array"); return; }
    if (sc->elem_type != dc->elem_type) { throw_new(t, "java/lang/ArrayStoreException", "arraycopy: type mismatch"); return; }
    if (n < 0 || sp < 0 || dp < 0 || sp > src->length - n || dp > dst->length - n) {
        throw_new(t, "java/lang/ArrayIndexOutOfBoundsException", "arraycopy: last source index out of bounds");
        return;
    }
    uint32_t size = sc->elem_size;
    if (sc->elem_type == T_REF && !is_assignable(sc->component, dc->component)) {
        Object **s = ADATA(src, Object *), **d = ADATA(dst, Object *);
        Class *e = dc->component;
        if (src == dst) { memmove(d + dp, s + sp, (size_t)n * size); return; }
        for (jint i = 0; i < n; i++) {
            Object *o = s[sp + i];
            if (o && !is_assignable(o->cls, e)) { throw_new(t, "java/lang/ArrayStoreException", "arraycopy: element type mismatch"); return; }
            d[dp + i] = o;
        }
        return;
    }
    memmove(ADATA(dst, uint8_t) + (size_t)dp * size, ADATA(src, uint8_t) + (size_t)sp * size, (size_t)n * size);
}
N(system_millis)
{
    UNUSED;
    /* Wall clock: the platform's epoch milliseconds. */
    extern jlong plat_epoch_ms(void);
    put_long(a, plat_epoch_ms());
}
N(system_nanos) { UNUSED; put_long(a, (jlong)plat_nanos()); }
N(system_identity) { (void)t; a[0].i = a[0].l ? (jint)((uintptr_t)a[0].l >> 3) * (jint)0x9e3779b1U : 0; }
N(system_exit) { (void)t; vm_exit(a[0].i); }
N(system_gc) { heap_collect(t); (void)a; }
N(system_write)
{
    (void)t;
    Array *b = (Array *)a[1].l;
    jint off = a[2].i, n = a[3].i;
    if (!b || off < 0 || n < 0 || off > b->length - n) return;
    plat_write(a[0].i == 2, (const char *)ADATA(b, uint8_t) + off, (uint32_t)n);
}
N(system_read_key)
{
    (void)t;
    a[0].i = plat_poll_key();
}
N(system_properties)
{
    char cwd[256] = "/", home[256];
    plat_getcwd(cwd, sizeof(cwd));
    const char *user = plat_user();
    if (vm_home[0]) snprintf(home, sizeof(home), "%s", vm_home);
    else if (!strcmp(user, "root")) snprintf(home, sizeof(home), "/root");
    else snprintf(home, sizeof(home), "/home/%s", user);
    char cpus[16];
    snprintf(cpus, sizeof(cpus), "%u", plat_cpus());
    const char *pairs[] = {
        "java.version", "1.8.0", "java.vendor", "Barnix", "java.vm.name", "BJVM",
        "java.vm.version", "0.1", "java.specification.version", "1.8", "java.class.version", "52.0",
        "os.name", plat_os(), "os.arch", "x86", "os.version", "0.3", "file.separator", "/",
        "path.separator", ":", "line.separator", "\n", "user.dir", cwd, "user.home", home,
        "user.name", user, "file.encoding", "UTF-8", "java.io.tmpdir", "/tmp",
        "sun.arch.data.model", "32", "bjvm.cpus", cpus, "java.home", "/lib/java",
    };
    uint32_t n = sizeof(pairs) / sizeof(pairs[0]);
    Array *r = new_array(t, vm.string_array, (jint)n);
    if (!r) return;
    a[0].l = &r->o;
    for (uint32_t i = 0; i < n; i++) {
        Object *s = new_string(t, pairs[i]);
        if (!s) return;
        ADATA(r, Object *)[i] = s;
    }
}
extern char **bjvm_defines;
extern int bjvm_define_count;
N(system_defines)
{
    Array *r = new_array(t, vm.string_array, bjvm_define_count);
    if (!r) return;
    a[0].l = &r->o;
    for (int i = 0; i < bjvm_define_count; i++) {
        Object *s = new_string(t, bjvm_defines[i]);
        if (!s) return;
        ADATA(r, Object *)[i] = s;
    }
}

/* ---- java.lang.Runtime ---- */
N(runtime_free) { UNUSED; put_long(a + 0, heap_free_bytes()); }
N(runtime_total) { UNUSED; put_long(a, heap_total_bytes()); }
N(runtime_processors) { UNUSED; a[0].i = (jint)plat_cpus(); }

/* ---- java.lang.Thread ---- */
N(thread_current_) { (void)t; a[0].l = t->jthread; }
N(thread_start_native)
{
    Object *j = a[0].l;
    Thread *child = thread_create(j, 1);
    if (!child) { throw_new(t, "java/lang/OutOfMemoryError", "unable to create new native thread"); return; }
    child->daemon = FIELD(j, vm.thread_daemon, jboolean);
    FIELD(j, vm.thread_vm, Thread *) = child;
    FIELD(j, vm.thread_alive, jboolean) = 1;
    thread_start(t, child);
}
N(thread_sleep_) { thread_sleep(t, get_long(a)); }
N(thread_yield_) { thread_yield(t); (void)a; }
static Thread *vm_thread(Object *j) { return j ? FIELD(j, vm.thread_vm, Thread *) : 0; }
N(thread_interrupt)
{
    (void)t;
    Thread *k = vm_thread(a[0].l);
    if (k) k->interrupted = 1;
}
N(thread_is_interrupted)
{
    (void)t;
    Thread *k = vm_thread(a[0].l);
    int v = k && k->interrupted;
    if (k && a[1].i) k->interrupted = 0;
    a[0].i = v;
}
N(thread_holds_lock) { if (!a[0].l) { throw_npe(t); return; } a[0].i = monitor_holds(t, a[0].l); }
N(thread_stop)
{
    Thread *k = vm_thread(a[0].l);
    if (!k) return;
    if (k == t) throw_new(t, "java/lang/ThreadDeath", 0);
    else { k->stop_requested = 1; if (k->state == TS_SLEEPING || k->state == TS_WAITING) k->interrupted = 1; }
}

/* ---- java.lang.Throwable ---- */
N(throwable_fill)
{
    Object *self = a[0].l;
    int skip = 0;
    for (Frame *f = t->fp; f > t->frames; f--) {
        Method *m = f->method;
        if (!m || !m->owner) continue;
        if (!strcmp(m->name, "fillInStackTrace") || (!strcmp(m->name, "<init>") && is_assignable(m->owner, vm.throwable)) ) { skip++; continue; }
        break;
    }
    FIELD(self, vm.throwable_trace, Object *) = make_trace(t, skip);
}
extern int method_line(Method *m, int pc);
N(throwable_trace)
{
    Object *self = a[0].l;
    Array *trace = FIELD(self, vm.throwable_trace, Array *);
    jint n = trace && trace->o.cls == vm.prim_arrays[T_INT] ? trace->length / 2 : 0;
    Class *ste = vm.stack_trace_element;
    if (init_class(t, ste)) return;
    Array *r = new_array(t, array_of(t, ste), n);
    if (!r) return;
    a[0].l = &r->o;
    uint32_t oc = field_offset(ste, "declaringClass"), om = field_offset(ste, "methodName"),
             of = field_offset(ste, "fileName"), ol = field_offset(ste, "lineNumber");
    for (jint i = 0; i < n; i++) {
        Method *m = (Method *)(uintptr_t)ADATA(trace, jint)[i * 2];
        Object *e = new_object(t, ste);
        if (!e) return;
        ADATA(r, Object *)[i] = e;
        char name[256];
        Object *s = new_string(t, class_java_name(m->owner, name, sizeof(name)));
        if (!s) return;
        FIELD(e, oc, Object *) = s;
        if (!(s = intern_utf8(t, m->name))) return;
        FIELD(e, om, Object *) = s;
        if (m->owner->source_file) { if (!(s = intern_utf8(t, m->owner->source_file))) return; FIELD(e, of, Object *) = s; }
        FIELD(e, ol, jint) = method_line(m, ADATA(trace, jint)[i * 2 + 1]);
    }
}

/* ---- java.lang.String ---- */
N(string_intern) { a[0].l = intern_string(t, a[0].l); }

/* ---- numbers ---- */
N(float_to_bits) { (void)t; (void)a; }
N(bits_to_float) { (void)t; (void)a; }
N(double_to_bits) { (void)t; (void)a; }
N(bits_to_double) { (void)t; (void)a; }

/* Java's shortest decimal that rounds back, in Java's layout. */
static void java_number(char *out, uint32_t size, double v, int is_float)
{
    if (v != v) { snprintf(out, size, "NaN"); return; }
    if (isinf(v)) { snprintf(out, size, v > 0 ? "Infinity" : "-Infinity"); return; }
    if (v == 0) { snprintf(out, size, signbit(v) ? "-0.0" : "0.0"); return; }
    char digits[40];
    int precision = 1;
    for (; precision <= 17; precision++) {
        snprintf(digits, sizeof(digits), "%.*e", precision - 1, v);
        if (is_float ? (float)strtod(digits, 0) == (float)v : strtod(digits, 0) == v) break;
    }
    /* digits: [-]d.ddde[+-]xx */
    char mant[24];
    int n = 0, neg = digits[0] == '-';
    const char *p = digits + neg;
    for (; *p && *p != 'e'; p++) if (*p != '.') mant[n++] = *p;
    mant[n] = 0;
    int exp = atoi(p + 1);
    while (n > 1 && mant[n - 1] == '0') mant[--n] = 0;
    uint32_t at = 0;
    if (neg) out[at++] = '-';
    if (exp >= -3 && exp < 7) {
        if (exp < 0) {
            out[at++] = '0'; out[at++] = '.';
            for (int i = -1; i > exp; i--) out[at++] = '0';
            for (int i = 0; i < n; i++) out[at++] = mant[i];
        } else {
            for (int i = 0; i <= exp; i++) out[at++] = i < n ? mant[i] : '0';
            out[at++] = '.';
            if (n > exp + 1) for (int i = exp + 1; i < n; i++) out[at++] = mant[i];
            else out[at++] = '0';
        }
        out[at] = 0;
    } else {
        out[at++] = mant[0]; out[at++] = '.';
        if (n > 1) for (int i = 1; i < n; i++) out[at++] = mant[i];
        else out[at++] = '0';
        snprintf(out + at, size - at, "E%d", exp);
    }
}
N(float_to_string) { char s[48]; java_number(s, sizeof(s), a[0].f, 1); a[0].l = new_string(t, s); }
N(double_to_string) { char s[48]; java_number(s, sizeof(s), get_double(a), 0); a[0].l = new_string(t, s); }
N(double_parse)
{
    char text[128], *end;
    if (!a[0].l) { throw_npe(t); return; }
    string_utf8(a[0].l, text, sizeof(text));
    char *s = text;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    uint32_t n = (uint32_t)strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\n' || s[n - 1] == '\r')) s[--n] = 0;
    if (n && (s[n - 1] == 'f' || s[n - 1] == 'F' || s[n - 1] == 'd' || s[n - 1] == 'D')) s[--n] = 0;
    double v;
    const char *body = s + (*s == '-' || *s == '+');
    if (!strcmp(body, "Infinity")) v = *s == '-' ? -INFINITY : INFINITY;
    else if (!strcmp(body, "NaN")) v = NAN;
    else {
        int ok = n > 0;
        for (const char *q = s; *q && ok; q++)
            if (!((*q >= '0' && *q <= '9') || *q == '.' || *q == 'e' || *q == 'E' || *q == '-' || *q == '+')) ok = 0;
        v = ok ? strtod(s, &end) : 0;
        if (!ok || *end) {
            char msg[160];
            snprintf(msg, sizeof(msg), n ? "For input string: \"%s\"" : "empty String", text);
            throw_new(t, "java/lang/NumberFormatException", msg);
            return;
        }
    }
    put_double(a, v);
}

/* ---- java.lang.Math ---- */
#define MATH1(name, fn) N(name) { (void)t; put_double(a, fn(get_double(a))); }
#define MATH2(name, fn) N(name) { (void)t; put_double(a, fn(get_double(a), get_double(a + 2))); }
MATH1(math_sin, sin) MATH1(math_cos, cos) MATH1(math_tan, tan) MATH1(math_asin, asin)
MATH1(math_acos, acos) MATH1(math_atan, atan) MATH1(math_exp, exp) MATH1(math_log, log)
MATH1(math_log10, log10) MATH1(math_sqrt, sqrt) MATH1(math_cbrt, cbrt) MATH1(math_sinh, sinh)
MATH1(math_cosh, cosh) MATH1(math_tanh, tanh) MATH1(math_expm1, expm1) MATH1(math_log1p, log1p)
MATH1(math_ceil, ceil) MATH1(math_floor, floor) MATH1(math_rint, rint)
MATH2(math_atan2, atan2) MATH2(math_pow, pow) MATH2(math_hypot, hypot) MATH2(math_ieee_rem, remainder)

/* ---- java.lang.reflect.Array ---- */
N(array_new)
{
    Class *c = mirror_class(a[0].l);
    if (c == vm.prims[0]) { throw_new(t, "java/lang/IllegalArgumentException", 0); return; }
    Array *r = new_object_array(t, c, a[1].i);
    a[0].l = r ? &r->o : 0;
}
static Array *array_arg(Thread *t, Object *o)
{
    if (!o) { throw_npe(t); return 0; }
    if (!(o->cls->flags & CF_ARRAY)) { throw_new(t, "java/lang/IllegalArgumentException", "Argument is not an array"); return 0; }
    return (Array *)o;
}
N(array_length) { Array *x = array_arg(t, a[0].l); if (x) a[0].i = x->length; }
static const char type_codes[] = "????ZCFDBSIJL";
N(array_get)
{
    Array *x = array_arg(t, a[0].l);
    if (!x) return;
    jint i = a[1].i;
    if (i < 0 || i >= x->length) { throw_new(t, "java/lang/ArrayIndexOutOfBoundsException", 0); return; }
    Slot v[2];
    switch (x->o.cls->elem_type) {
    case T_REF: a[0].l = ADATA(x, Object *)[i]; return;
    case T_BOOLEAN: case T_BYTE: v[0].i = ADATA(x, int8_t)[i]; break;
    case T_CHAR: v[0].i = ADATA(x, uint16_t)[i]; break;
    case T_SHORT: v[0].i = ADATA(x, int16_t)[i]; break;
    case T_INT: case T_FLOAT: v[0].i = ADATA(x, jint)[i]; break;
    default: memcpy(v, ADATA(x, uint64_t) + i, 8);
    }
    a[0].l = box(t, type_codes[x->o.cls->elem_type], v);
}
N(array_set)
{
    Array *x = array_arg(t, a[0].l);
    if (!x) return;
    jint i = a[1].i;
    if (i < 0 || i >= x->length) { throw_new(t, "java/lang/ArrayIndexOutOfBoundsException", 0); return; }
    if (x->o.cls->elem_type == T_REF) {
        Object *v = a[2].l;
        if (v && !is_assignable(v->cls, x->o.cls->component)) { throw_new(t, "java/lang/IllegalArgumentException", "array element type mismatch"); return; }
        ADATA(x, Object *)[i] = v;
        return;
    }
    Slot v[2];
    if (unbox(t, a[2].l, type_codes[x->o.cls->elem_type], v) < 0) return;
    switch (x->o.cls->elem_type) {
    case T_BOOLEAN: case T_BYTE: ADATA(x, int8_t)[i] = (int8_t)v[0].i; break;
    case T_CHAR: case T_SHORT: ADATA(x, uint16_t)[i] = (uint16_t)v[0].i; break;
    case T_INT: case T_FLOAT: ADATA(x, jint)[i] = v[0].i; break;
    default: memcpy(ADATA(x, uint64_t) + i, v, 8);
    }
}

const NativeEntry natives_core[] = {
    {"java/lang/reflect/Array", "newArray0", "(Ljava/lang/Class;I)Ljava/lang/Object;", array_new},
    {"java/lang/reflect/Array", "getLength", "(Ljava/lang/Object;)I", array_length},
    {"java/lang/reflect/Array", "get", "(Ljava/lang/Object;I)Ljava/lang/Object;", array_get},
    {"java/lang/reflect/Array", "set", "(Ljava/lang/Object;ILjava/lang/Object;)V", array_set},
    {"java/lang/Object", "getClass", "()Ljava/lang/Class;", object_get_class},
    {"java/lang/Object", "hashCode", "()I", object_hash},
    {"java/lang/Object", "clone", "()Ljava/lang/Object;", object_clone},
    {"java/lang/Object", "notify", "()V", object_notify},
    {"java/lang/Object", "notifyAll", "()V", object_notify_all},
    {"java/lang/Object", "wait", "(J)V", object_wait},
    {"java/lang/Class", "getName0", "()Ljava/lang/String;", class_name},
    {"java/lang/Class", "forName0", "(Ljava/lang/String;Z)Ljava/lang/Class;", class_for_name},
    {"java/lang/Class", "isInstance", "(Ljava/lang/Object;)Z", class_is_instance},
    {"java/lang/Class", "isAssignableFrom", "(Ljava/lang/Class;)Z", class_is_assignable},
    {"java/lang/Class", "isInterface", "()Z", class_is_interface},
    {"java/lang/Class", "isArray", "()Z", class_is_array},
    {"java/lang/Class", "isPrimitive", "()Z", class_is_primitive},
    {"java/lang/Class", "getModifiers", "()I", class_modifiers},
    {"java/lang/Class", "getSuperclass", "()Ljava/lang/Class;", class_super},
    {"java/lang/Class", "getComponentType", "()Ljava/lang/Class;", class_component},
    {"java/lang/Class", "getInterfaces", "()[Ljava/lang/Class;", class_interfaces},
    {"java/lang/Class", "getPrimitiveClass", "(Ljava/lang/String;)Ljava/lang/Class;", class_primitive},
    {"java/lang/Class", "initialize0", "()V", class_init},
    {"java/lang/Class", "getResourceBytes", "(Ljava/lang/String;)[B", class_resource_bytes},
    {"java/lang/Class", "members0", "(ZZ)[Ljava/lang/Object;", class_members},
    {"java/lang/Class", "enumConstants0", "()[Ljava/lang/Object;", class_enum_constants},
    {"java/lang/reflect/Constructor", "newInstance0", "([Ljava/lang/Object;)Ljava/lang/Object;", ctor_new},
    {"java/lang/reflect/Method", "invoke0", "(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;", method_invoke},
    {"java/lang/System", "arraycopy", "(Ljava/lang/Object;ILjava/lang/Object;II)V", system_arraycopy},
    {"java/lang/System", "currentTimeMillis", "()J", system_millis},
    {"java/lang/System", "nanoTime", "()J", system_nanos},
    {"java/lang/System", "identityHashCode", "(Ljava/lang/Object;)I", system_identity},
    {"java/lang/System", "exit0", "(I)V", system_exit},
    {"java/lang/System", "gc0", "()V", system_gc},
    {"java/lang/System", "write0", "(I[BII)V", system_write},
    {"java/lang/System", "readKey0", "()I", system_read_key},
    {"java/lang/System", "platformProperties", "()[Ljava/lang/String;", system_properties},
    {"java/lang/System", "commandLineDefines", "()[Ljava/lang/String;", system_defines},
    {"java/lang/Runtime", "freeMemory", "()J", runtime_free},
    {"java/lang/Runtime", "totalMemory", "()J", runtime_total},
    {"java/lang/Runtime", "maxMemory", "()J", runtime_total},
    {"java/lang/Runtime", "availableProcessors", "()I", runtime_processors},
    {"java/lang/Thread", "currentThread", "()Ljava/lang/Thread;", thread_current_},
    {"java/lang/Thread", "start0", "()V", thread_start_native},
    {"java/lang/Thread", "sleep0", "(J)V", thread_sleep_},
    {"java/lang/Thread", "yield", "()V", thread_yield_},
    {"java/lang/Thread", "interrupt0", "()V", thread_interrupt},
    {"java/lang/Thread", "isInterrupted0", "(Z)Z", thread_is_interrupted},
    {"java/lang/Thread", "holdsLock", "(Ljava/lang/Object;)Z", thread_holds_lock},
    {"java/lang/Thread", "stop0", "()V", thread_stop},
    {"java/lang/Throwable", "fillInStackTrace0", "()V", throwable_fill},
    {"java/lang/Throwable", "getStackTrace0", "()[Ljava/lang/StackTraceElement;", throwable_trace},
    {"java/lang/String", "intern", "()Ljava/lang/String;", string_intern},
    {"java/lang/Float", "floatToRawIntBits", "(F)I", float_to_bits},
    {"java/lang/Float", "intBitsToFloat", "(I)F", bits_to_float},
    {"java/lang/Double", "doubleToRawLongBits", "(D)J", double_to_bits},
    {"java/lang/Double", "longBitsToDouble", "(J)D", bits_to_double},
    {"java/lang/Float", "toString", "(F)Ljava/lang/String;", float_to_string},
    {"java/lang/Double", "toString", "(D)Ljava/lang/String;", double_to_string},
    {"java/lang/Double", "parse0", "(Ljava/lang/String;)D", double_parse},
    {"java/lang/Math", "sin", "(D)D", math_sin}, {"java/lang/Math", "cos", "(D)D", math_cos},
    {"java/lang/Math", "tan", "(D)D", math_tan}, {"java/lang/Math", "asin", "(D)D", math_asin},
    {"java/lang/Math", "acos", "(D)D", math_acos}, {"java/lang/Math", "atan", "(D)D", math_atan},
    {"java/lang/Math", "exp", "(D)D", math_exp}, {"java/lang/Math", "log", "(D)D", math_log},
    {"java/lang/Math", "log10", "(D)D", math_log10}, {"java/lang/Math", "sqrt", "(D)D", math_sqrt},
    {"java/lang/Math", "cbrt", "(D)D", math_cbrt}, {"java/lang/Math", "sinh", "(D)D", math_sinh},
    {"java/lang/Math", "cosh", "(D)D", math_cosh}, {"java/lang/Math", "tanh", "(D)D", math_tanh},
    {"java/lang/Math", "expm1", "(D)D", math_expm1}, {"java/lang/Math", "log1p", "(D)D", math_log1p},
    {"java/lang/Math", "ceil", "(D)D", math_ceil}, {"java/lang/Math", "floor", "(D)D", math_floor},
    {"java/lang/Math", "rint", "(D)D", math_rint}, {"java/lang/Math", "atan2", "(DD)D", math_atan2},
    {"java/lang/Math", "pow", "(DD)D", math_pow}, {"java/lang/Math", "hypot", "(DD)D", math_hypot},
    {"java/lang/Math", "IEEEremainder", "(DD)D", math_ieee_rem},
    {0, 0, 0, 0}
};
