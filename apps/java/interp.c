/* The bytecode interpreter: direct-threaded dispatch over the original
 * bytecode, Java-to-Java calls without C recursion, inline caches for
 * interface and miranda calls. Calls into Java from C (class
 * initializers, natives, thread start) nest through invoke(). */
#include "jvm.h"
#include <math.h>
#include <stdio.h>

static inline int insn_length_invoke(uint8_t op) { return op == 0xb9 || op == 0xba ? 5 : 3; }
static inline uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static inline int16_t s16(const uint8_t *p) { return (int16_t)(p[0] << 8 | p[1]); }
static inline int32_t s32(const uint8_t *p) { return (int32_t)((uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]); }

static jint d2i(jdouble d)
{
    if (d != d) return 0;
    if (d >= 2147483647.0) return 2147483647;
    if (d <= -2147483648.0) return (jint)0x80000000U;
    return (jint)d;
}
static jlong d2l(jdouble d)
{
    if (d != d) return 0;
    if (d >= 9223372036854775807.0) return INT64_MAX;
    if (d <= -9223372036854775808.0) return INT64_MIN;
    return (jlong)d;
}

Thread *current_thread(void) { return thread_current; }

static void bind_native(Thread *t, Method *m)
{
    if (!m->bound) { m->native = native_lookup(m); m->bound = 1; }
    if (!m->native) {
        char text[600], name[256];
        snprintf(text, sizeof(text), "%s.%s%s", class_java_name(m->owner, name, sizeof(name)), m->name, m->desc);
        throw_new(t, "java/lang/UnsatisfiedLinkError", text);
    }
}

static Slot *frame_top(Thread *t)
{
    Frame *f = t->fp;
    return f->method ? f->base + f->method->max_stack : t->stack;
}

static int ret_slots(char r) { return r == 'V' ? 0 : r == 'J' || r == 'D' ? 2 : 1; }

/* Headroom kept below every nested invoke for natives (OpenGL, inflate). */
#define C_STACK_RESERVE (64U * 1024U)

int invoke(Thread *t, Method *m, Slot *args, Slot *result)
{
    /* Natives, class initializers and reflection re-enter the interpreter
     * through this C function. The C stack is plain heap memory: running
     * past its end would corrupt the heap, so report it to Java instead. */
    volatile char probe;
    uintptr_t here = (uintptr_t)&probe, low = (uintptr_t)t->c_stack;
    if (t->c_stack && here >= low && here - low < C_STACK_RESERVE) {
        throw_new(t, "java/lang/StackOverflowError", "native stack");
        return -1;
    }
    if (m->access & ACC_NATIVE) {
        bind_native(t, m);
        if (!m->native) return -1;
        /* Arguments live on this C stack, which the collector scans. */
        Slot place[256];
        if (m->arg_slots) memcpy(place, args, m->arg_slots * sizeof(Slot));
        Frame *saved = t->fp;
        Object *lock = 0;
        if (m->access & ACC_SYNCHRONIZED) {
            lock = m->access & ACC_STATIC ? class_mirror(t, m->owner) : place[0].l;
            monitor_enter(t, lock);
        }
        m->native(t, place);
        if (lock) monitor_exit(t, lock);
        t->fp = saved;
        if (result) memcpy(result, place, (size_t)ret_slots(m->ret) * sizeof(Slot));
        return t->exception ? -1 : 0;
    }
    if (!m->code) {
        char text[600], name[256];
        snprintf(text, sizeof(text), "%s.%s%s", class_java_name(m->owner, name, sizeof(name)), m->name, m->desc);
        throw_new(t, "java/lang/AbstractMethodError", text);
        return -1;
    }
    Slot *locals = frame_top(t);
    if (t->fp + 1 >= t->frames_end || locals + m->max_locals + m->max_stack > t->stack_end) {
        throw_new(t, "java/lang/StackOverflowError", 0);
        return -1;
    }
    if (m->arg_slots) memcpy(locals, args, m->arg_slots * sizeof(Slot));
    Frame *f = t->fp + 1;
    f->method = m;
    f->locals = locals;
    f->base = locals + m->max_locals;
    f->sp = f->base;
    f->pc = m->code;
    f->cp = &m->owner->cp;
    f->entry = 1;
    f->lock = 0;
    t->fp = f;
    if (m->access & ACC_SYNCHRONIZED) {
        f->lock = m->access & ACC_STATIC ? class_mirror(t, m->owner) : locals[0].l;
        monitor_enter(t, f->lock);
    }
    interpret(t);
    if (t->exception) return -1;
    if (result) memcpy(result, locals, (size_t)ret_slots(m->ret) * sizeof(Slot));
    return 0;
}

static Object *mirror_or_null(Thread *t, Class *c) { return class_mirror(t, c); }

static Array *multi_array(Thread *t, Class *c, Slot *dims, int count)
{
    jint n = dims[0].i;
    Array *a = new_array(t, c, n);
    if (!a || count == 1) return a;
    for (jint i = 0; i < n; i++) {
        Array *sub = multi_array(t, c->component, dims + 1, count - 1);
        if (!sub) return 0;
        ADATA(a, Object *)[i] = &sub->o;
    }
    return a;
}

void interpret(Thread *t)
{
    static void *labels[256] = {
        [0 ... 255] = &&op_bad,
        [0x00] = &&op_nop, [0x01] = &&op_aconst_null, [0x02] = &&op_iconst, [0x03] = &&op_iconst,
        [0x04] = &&op_iconst, [0x05] = &&op_iconst, [0x06] = &&op_iconst, [0x07] = &&op_iconst,
        [0x08] = &&op_iconst, [0x09] = &&op_lconst, [0x0a] = &&op_lconst, [0x0b] = &&op_fconst,
        [0x0c] = &&op_fconst, [0x0d] = &&op_fconst, [0x0e] = &&op_dconst, [0x0f] = &&op_dconst,
        [0x10] = &&op_bipush, [0x11] = &&op_sipush, [0x12] = &&op_ldc, [0x13] = &&op_ldc_w,
        [0x14] = &&op_ldc2_w, [0x15] = &&op_iload, [0x16] = &&op_lload, [0x17] = &&op_iload,
        [0x18] = &&op_lload, [0x19] = &&op_iload,
        [0x1a] = &&op_iload_n, [0x1b] = &&op_iload_n, [0x1c] = &&op_iload_n, [0x1d] = &&op_iload_n,
        [0x1e] = &&op_lload_n, [0x1f] = &&op_lload_n, [0x20] = &&op_lload_n, [0x21] = &&op_lload_n,
        [0x22] = &&op_fload_n, [0x23] = &&op_fload_n, [0x24] = &&op_fload_n, [0x25] = &&op_fload_n,
        [0x26] = &&op_dload_n, [0x27] = &&op_dload_n, [0x28] = &&op_dload_n, [0x29] = &&op_dload_n,
        [0x2a] = &&op_aload_n, [0x2b] = &&op_aload_n, [0x2c] = &&op_aload_n, [0x2d] = &&op_aload_n,
        [0x2e] = &&op_iaload, [0x2f] = &&op_laload, [0x30] = &&op_iaload, [0x31] = &&op_laload,
        [0x32] = &&op_iaload, [0x33] = &&op_baload, [0x34] = &&op_caload, [0x35] = &&op_saload,
        [0x36] = &&op_istore, [0x37] = &&op_lstore, [0x38] = &&op_istore, [0x39] = &&op_lstore,
        [0x3a] = &&op_istore,
        [0x3b] = &&op_istore_n, [0x3c] = &&op_istore_n, [0x3d] = &&op_istore_n, [0x3e] = &&op_istore_n,
        [0x3f] = &&op_lstore_n, [0x40] = &&op_lstore_n, [0x41] = &&op_lstore_n, [0x42] = &&op_lstore_n,
        [0x43] = &&op_fstore_n, [0x44] = &&op_fstore_n, [0x45] = &&op_fstore_n, [0x46] = &&op_fstore_n,
        [0x47] = &&op_dstore_n, [0x48] = &&op_dstore_n, [0x49] = &&op_dstore_n, [0x4a] = &&op_dstore_n,
        [0x4b] = &&op_astore_n, [0x4c] = &&op_astore_n, [0x4d] = &&op_astore_n, [0x4e] = &&op_astore_n,
        [0x4f] = &&op_iastore, [0x50] = &&op_lastore, [0x51] = &&op_iastore, [0x52] = &&op_lastore,
        [0x53] = &&op_aastore, [0x54] = &&op_bastore, [0x55] = &&op_castore, [0x56] = &&op_castore,
        [0x57] = &&op_pop, [0x58] = &&op_pop2, [0x59] = &&op_dup, [0x5a] = &&op_dup_x1,
        [0x5b] = &&op_dup_x2, [0x5c] = &&op_dup2, [0x5d] = &&op_dup2_x1, [0x5e] = &&op_dup2_x2,
        [0x5f] = &&op_swap,
        [0x60] = &&op_iadd, [0x61] = &&op_ladd, [0x62] = &&op_fadd, [0x63] = &&op_dadd,
        [0x64] = &&op_isub, [0x65] = &&op_lsub, [0x66] = &&op_fsub, [0x67] = &&op_dsub,
        [0x68] = &&op_imul, [0x69] = &&op_lmul, [0x6a] = &&op_fmul, [0x6b] = &&op_dmul,
        [0x6c] = &&op_idiv, [0x6d] = &&op_ldiv, [0x6e] = &&op_fdiv, [0x6f] = &&op_ddiv,
        [0x70] = &&op_irem, [0x71] = &&op_lrem, [0x72] = &&op_frem, [0x73] = &&op_drem,
        [0x74] = &&op_ineg, [0x75] = &&op_lneg, [0x76] = &&op_fneg, [0x77] = &&op_dneg,
        [0x78] = &&op_ishl, [0x79] = &&op_lshl, [0x7a] = &&op_ishr, [0x7b] = &&op_lshr,
        [0x7c] = &&op_iushr, [0x7d] = &&op_lushr, [0x7e] = &&op_iand, [0x7f] = &&op_land,
        [0x80] = &&op_ior, [0x81] = &&op_lor, [0x82] = &&op_ixor, [0x83] = &&op_lxor,
        [0x84] = &&op_iinc,
        [0x85] = &&op_i2l, [0x86] = &&op_i2f, [0x87] = &&op_i2d, [0x88] = &&op_l2i,
        [0x89] = &&op_l2f, [0x8a] = &&op_l2d, [0x8b] = &&op_f2i, [0x8c] = &&op_f2l,
        [0x8d] = &&op_f2d, [0x8e] = &&op_d2i, [0x8f] = &&op_d2l, [0x90] = &&op_d2f,
        [0x91] = &&op_i2b, [0x92] = &&op_i2c, [0x93] = &&op_i2s,
        [0x94] = &&op_lcmp, [0x95] = &&op_fcmpl, [0x96] = &&op_fcmpg, [0x97] = &&op_dcmpl,
        [0x98] = &&op_dcmpg,
        [0x99] = &&op_ifeq, [0x9a] = &&op_ifne, [0x9b] = &&op_iflt, [0x9c] = &&op_ifge,
        [0x9d] = &&op_ifgt, [0x9e] = &&op_ifle,
        [0x9f] = &&op_if_icmpeq, [0xa0] = &&op_if_icmpne, [0xa1] = &&op_if_icmplt,
        [0xa2] = &&op_if_icmpge, [0xa3] = &&op_if_icmpgt, [0xa4] = &&op_if_icmple,
        [0xa5] = &&op_if_icmpeq, [0xa6] = &&op_if_icmpne,
        [0xa7] = &&op_goto, [0xa8] = &&op_jsr, [0xa9] = &&op_ret,
        [0xaa] = &&op_tableswitch, [0xab] = &&op_lookupswitch,
        [0xac] = &&op_ireturn, [0xad] = &&op_lreturn, [0xae] = &&op_ireturn, [0xaf] = &&op_lreturn,
        [0xb0] = &&op_ireturn, [0xb1] = &&op_return,
        [0xb2] = &&op_getstatic, [0xb3] = &&op_putstatic, [0xb4] = &&op_getfield, [0xb5] = &&op_putfield,
        [0xb6] = &&op_invokevirtual, [0xb7] = &&op_invokespecial, [0xb8] = &&op_invokestatic,
        [0xb9] = &&op_invokeinterface,
        [0xbb] = &&op_new, [0xbc] = &&op_newarray, [0xbd] = &&op_anewarray, [0xbe] = &&op_arraylength,
        [0xbf] = &&op_athrow, [0xc0] = &&op_checkcast, [0xc1] = &&op_instanceof,
        [0xc2] = &&op_monitorenter, [0xc3] = &&op_monitorexit, [0xc4] = &&op_wide,
        [0xc5] = &&op_multianewarray, [0xc6] = &&op_ifnull, [0xc7] = &&op_ifnonnull,
        [0xc8] = &&op_goto_w, [0xc9] = &&op_jsr_w,
    };
    Frame *fp;
    Method *m;
    uint8_t *pc;
    Slot *sp, *locals;
    ConstPool *cp;
    Class *cls;
    Method *target;
    jint ia, ib;
    jlong la, lb;
    Object *o;
    Array *arr;

#define LOAD_FRAME() (fp = t->fp, m = fp->method, pc = fp->pc, sp = fp->sp, locals = fp->locals, cp = fp->cp, cls = m->owner)
#define SAVE() (fp->pc = pc, fp->sp = sp)
#define DISPATCH() goto *labels[*pc]
#define NEXT(n) do { pc += (n); DISPATCH(); } while (0)
#define THROW(name, msg) do { SAVE(); throw_new(t, name, msg); goto exception; } while (0)
#define NPE() do { SAVE(); throw_npe(t); goto exception; } while (0)
#define CHECK() do { if (t->exception) goto exception; } while (0)
#define TICK() do { if (--t->ticks <= 0) { SAVE(); thread_tick(t); CHECK(); } } while (0)
#define BRANCH(cond, offset) do { if (cond) { int16_t o_ = (offset); if (o_ <= 0) TICK(); pc += o_; DISPATCH(); } NEXT(3); } while (0)
#define ARRAY_CHECK(a, i) do { if (!(a)) NPE(); if ((uint32_t)(i) >= (uint32_t)(a)->length) { char b_[24]; snprintf(b_, sizeof(b_), "%d", (int)(i)); THROW("java/lang/ArrayIndexOutOfBoundsException", b_); } } while (0)

    LOAD_FRAME();
    DISPATCH();

op_nop: NEXT(1);
op_aconst_null: (sp++)->l = 0; NEXT(1);
op_iconst: (sp++)->i = (jint)*pc - 3; NEXT(1);
op_lconst: put_long(sp, *pc - 9); sp += 2; NEXT(1);
op_fconst: (sp++)->f = (jfloat)(*pc - 0x0b); NEXT(1);
op_dconst: put_double(sp, (jdouble)(*pc - 0x0e)); sp += 2; NEXT(1);
op_bipush: (sp++)->i = (int8_t)pc[1]; NEXT(2);
op_sipush: (sp++)->i = s16(pc + 1); NEXT(3);
op_ldc: ia = pc[1]; goto ldc;
op_ldc_w: ia = u16(pc + 1);
ldc:
    switch (cp->tags[ia]) {
    case CP_INT: case CP_FLOAT: (sp++)->u = cp->data[ia]; break;
    case CP_STRING:
        SAVE();
        o = resolve_string(t, cls, (uint16_t)ia);
        CHECK();
        (sp++)->l = o;
        break;
    case CP_CLASS: {
        SAVE();
        Class *k = resolve_class(t, cls, (uint16_t)ia);
        CHECK();
        o = mirror_or_null(t, k);
        CHECK();
        (sp++)->l = o;
        break;
    }
    default: THROW("java/lang/VerifyError", "unsupported constant");
    }
    NEXT(*pc == 0x12 ? 2 : 3);
op_ldc2_w:
    ia = u16(pc + 1);
    { uint64_t v = (uint64_t)cp->data[ia] << 32 | cp->data[ia + 1]; __builtin_memcpy(sp, &v, 8); sp += 2; }
    NEXT(3);
op_iload: (sp++)->u = locals[pc[1]].u; NEXT(2);
op_lload: sp[0] = locals[pc[1]]; sp[1] = locals[pc[1] + 1]; sp += 2; NEXT(2);
op_iload_n: (sp++)->u = locals[*pc - 0x1a].u; NEXT(1);
op_lload_n: ia = *pc - 0x1e; sp[0] = locals[ia]; sp[1] = locals[ia + 1]; sp += 2; NEXT(1);
op_fload_n: (sp++)->u = locals[*pc - 0x22].u; NEXT(1);
op_dload_n: ia = *pc - 0x26; sp[0] = locals[ia]; sp[1] = locals[ia + 1]; sp += 2; NEXT(1);
op_aload_n: (sp++)->u = locals[*pc - 0x2a].u; NEXT(1);

op_iaload:  /* iaload, faload, aaload: 32-bit elements */
    arr = (Array *)sp[-2].l; ia = sp[-1].i;
    ARRAY_CHECK(arr, ia);
    sp[-2].u = ADATA(arr, uint32_t)[ia]; sp--; NEXT(1);
op_laload:
    arr = (Array *)sp[-2].l; ia = sp[-1].i;
    ARRAY_CHECK(arr, ia);
    __builtin_memcpy(sp - 2, ADATA(arr, uint64_t) + ia, 8); NEXT(1);
op_baload:
    arr = (Array *)sp[-2].l; ia = sp[-1].i;
    ARRAY_CHECK(arr, ia);
    sp[-2].i = ADATA(arr, int8_t)[ia]; sp--; NEXT(1);
op_caload:
    arr = (Array *)sp[-2].l; ia = sp[-1].i;
    ARRAY_CHECK(arr, ia);
    sp[-2].i = ADATA(arr, uint16_t)[ia]; sp--; NEXT(1);
op_saload:
    arr = (Array *)sp[-2].l; ia = sp[-1].i;
    ARRAY_CHECK(arr, ia);
    sp[-2].i = ADATA(arr, int16_t)[ia]; sp--; NEXT(1);

op_istore: locals[pc[1]] = *--sp; NEXT(2);
op_lstore: sp -= 2; locals[pc[1]] = sp[0]; locals[pc[1] + 1] = sp[1]; NEXT(2);
op_istore_n: locals[*pc - 0x3b] = *--sp; NEXT(1);
op_lstore_n: ia = *pc - 0x3f; sp -= 2; locals[ia] = sp[0]; locals[ia + 1] = sp[1]; NEXT(1);
op_fstore_n: locals[*pc - 0x43] = *--sp; NEXT(1);
op_dstore_n: ia = *pc - 0x47; sp -= 2; locals[ia] = sp[0]; locals[ia + 1] = sp[1]; NEXT(1);
op_astore_n: locals[*pc - 0x4b] = *--sp; NEXT(1);

op_iastore:
    arr = (Array *)sp[-3].l; ia = sp[-2].i;
    ARRAY_CHECK(arr, ia);
    ADATA(arr, uint32_t)[ia] = sp[-1].u; sp -= 3; NEXT(1);
op_lastore:
    arr = (Array *)sp[-4].l; ia = sp[-3].i;
    ARRAY_CHECK(arr, ia);
    __builtin_memcpy(ADATA(arr, uint64_t) + ia, sp - 2, 8); sp -= 4; NEXT(1);
op_aastore:
    arr = (Array *)sp[-3].l; ia = sp[-2].i; o = sp[-1].l;
    ARRAY_CHECK(arr, ia);
    if (o && !is_assignable(o->cls, arr->o.cls->component)) {
        char name[256];
        THROW("java/lang/ArrayStoreException", class_java_name(o->cls, name, sizeof(name)));
    }
    ADATA(arr, Object *)[ia] = o; sp -= 3; NEXT(1);
op_bastore:
    arr = (Array *)sp[-3].l; ia = sp[-2].i;
    ARRAY_CHECK(arr, ia);
    ADATA(arr, int8_t)[ia] = (int8_t)(arr->o.cls->elem_type == T_BOOLEAN ? sp[-1].i & 1 : sp[-1].i);
    sp -= 3; NEXT(1);
op_castore:
    arr = (Array *)sp[-3].l; ia = sp[-2].i;
    ARRAY_CHECK(arr, ia);
    ADATA(arr, uint16_t)[ia] = (uint16_t)sp[-1].i; sp -= 3; NEXT(1);

op_pop: sp--; NEXT(1);
op_pop2: sp -= 2; NEXT(1);
op_dup: sp[0] = sp[-1]; sp++; NEXT(1);
op_dup_x1: { Slot a = sp[-1], b = sp[-2]; sp[-2] = a; sp[-1] = b; sp[0] = a; sp++; } NEXT(1);
op_dup_x2: { Slot a = sp[-1], b = sp[-2], c = sp[-3]; sp[-3] = a; sp[-2] = c; sp[-1] = b; sp[0] = a; sp++; } NEXT(1);
op_dup2: sp[0] = sp[-2]; sp[1] = sp[-1]; sp += 2; NEXT(1);
op_dup2_x1: { Slot a = sp[-1], b = sp[-2], c = sp[-3];
    sp[-3] = b; sp[-2] = a; sp[-1] = c; sp[0] = b; sp[1] = a; sp += 2; } NEXT(1);
op_dup2_x2: { Slot a = sp[-1], b = sp[-2], c = sp[-3], d = sp[-4];
    sp[-4] = b; sp[-3] = a; sp[-2] = d; sp[-1] = c; sp[0] = b; sp[1] = a; sp += 2; } NEXT(1);
op_swap: { Slot a = sp[-1]; sp[-1] = sp[-2]; sp[-2] = a; } NEXT(1);

#define IBIN(op) sp[-2].i = (jint)((uint32_t)sp[-2].i op (uint32_t)sp[-1].i); sp--; NEXT(1)
#define LBIN(op) la = get_long(sp - 4); lb = get_long(sp - 2); put_long(sp - 4, (jlong)((uint64_t)la op (uint64_t)lb)); sp -= 2; NEXT(1)
#define FBIN(op) sp[-2].f = sp[-2].f op sp[-1].f; sp--; NEXT(1)
#define DBIN(op) put_double(sp - 4, get_double(sp - 4) op get_double(sp - 2)); sp -= 2; NEXT(1)
op_iadd: IBIN(+);
op_ladd: LBIN(+);
op_fadd: FBIN(+);
op_dadd: DBIN(+);
op_isub: IBIN(-);
op_lsub: LBIN(-);
op_fsub: FBIN(-);
op_dsub: DBIN(-);
op_imul: IBIN(*);
op_lmul: LBIN(*);
op_fmul: FBIN(*);
op_dmul: DBIN(*);
op_idiv:
    ib = sp[-1].i; ia = sp[-2].i;
    if (!ib) THROW("java/lang/ArithmeticException", "/ by zero");
    sp[-2].i = ib == -1 ? (jint)(0U - (uint32_t)ia) : ia / ib; sp--; NEXT(1);
op_ldiv:
    lb = get_long(sp - 2); la = get_long(sp - 4);
    if (!lb) THROW("java/lang/ArithmeticException", "/ by zero");
    put_long(sp - 4, lb == -1 ? (jlong)(0ULL - (uint64_t)la) : la / lb); sp -= 2; NEXT(1);
op_fdiv: FBIN(/);
op_ddiv: DBIN(/);
op_irem:
    ib = sp[-1].i; ia = sp[-2].i;
    if (!ib) THROW("java/lang/ArithmeticException", "/ by zero");
    sp[-2].i = ib == -1 ? 0 : ia % ib; sp--; NEXT(1);
op_lrem:
    lb = get_long(sp - 2); la = get_long(sp - 4);
    if (!lb) THROW("java/lang/ArithmeticException", "/ by zero");
    put_long(sp - 4, lb == -1 ? 0 : la % lb); sp -= 2; NEXT(1);
op_frem: sp[-2].f = (jfloat)fmod(sp[-2].f, sp[-1].f); sp--; NEXT(1);
op_drem: put_double(sp - 4, fmod(get_double(sp - 4), get_double(sp - 2))); sp -= 2; NEXT(1);
op_ineg: sp[-1].i = (jint)(0U - (uint32_t)sp[-1].i); NEXT(1);
op_lneg: put_long(sp - 2, (jlong)(0ULL - (uint64_t)get_long(sp - 2))); NEXT(1);
op_fneg: sp[-1].f = -sp[-1].f; NEXT(1);
op_dneg: put_double(sp - 2, -get_double(sp - 2)); NEXT(1);
op_ishl: sp[-2].i = (jint)((uint32_t)sp[-2].i << (sp[-1].i & 31)); sp--; NEXT(1);
op_lshl: put_long(sp - 3, (jlong)((uint64_t)get_long(sp - 3) << (sp[-1].i & 63))); sp--; NEXT(1);
op_ishr: sp[-2].i = sp[-2].i >> (sp[-1].i & 31); sp--; NEXT(1);
op_lshr: put_long(sp - 3, get_long(sp - 3) >> (sp[-1].i & 63)); sp--; NEXT(1);
op_iushr: sp[-2].u = sp[-2].u >> (sp[-1].i & 31); sp--; NEXT(1);
op_lushr: put_long(sp - 3, (jlong)((uint64_t)get_long(sp - 3) >> (sp[-1].i & 63))); sp--; NEXT(1);
op_iand: IBIN(&);
op_land: LBIN(&);
op_ior: IBIN(|);
op_lor: LBIN(|);
op_ixor: IBIN(^);
op_lxor: LBIN(^);
op_iinc: locals[pc[1]].i += (int8_t)pc[2]; NEXT(3);

op_i2l: put_long(sp - 1, sp[-1].i); sp++; NEXT(1);
op_i2f: sp[-1].f = (jfloat)sp[-1].i; NEXT(1);
op_i2d: put_double(sp - 1, sp[-1].i); sp++; NEXT(1);
op_l2i: sp[-2].i = (jint)get_long(sp - 2); sp--; NEXT(1);
op_l2f: sp[-2].f = (jfloat)get_long(sp - 2); sp--; NEXT(1);
op_l2d: put_double(sp - 2, (jdouble)get_long(sp - 2)); NEXT(1);
op_f2i: sp[-1].i = d2i(sp[-1].f); NEXT(1);
op_f2l: put_long(sp - 1, d2l(sp[-1].f)); sp++; NEXT(1);
op_f2d: put_double(sp - 1, sp[-1].f); sp++; NEXT(1);
op_d2i: sp[-2].i = d2i(get_double(sp - 2)); sp--; NEXT(1);
op_d2l: put_long(sp - 2, d2l(get_double(sp - 2))); NEXT(1);
op_d2f: sp[-2].f = (jfloat)get_double(sp - 2); sp--; NEXT(1);
op_i2b: sp[-1].i = (int8_t)sp[-1].i; NEXT(1);
op_i2c: sp[-1].i = (uint16_t)sp[-1].i; NEXT(1);
op_i2s: sp[-1].i = (int16_t)sp[-1].i; NEXT(1);

op_lcmp: la = get_long(sp - 4); lb = get_long(sp - 2); sp -= 3; sp[-1].i = la < lb ? -1 : la > lb; NEXT(1);
op_fcmpl: { jfloat a = sp[-2].f, b = sp[-1].f; sp--; sp[-1].i = a > b ? 1 : a == b ? 0 : -1; } NEXT(1);
op_fcmpg: { jfloat a = sp[-2].f, b = sp[-1].f; sp--; sp[-1].i = a < b ? -1 : a == b ? 0 : 1; } NEXT(1);
op_dcmpl: { jdouble a = get_double(sp - 4), b = get_double(sp - 2); sp -= 3; sp[-1].i = a > b ? 1 : a == b ? 0 : -1; } NEXT(1);
op_dcmpg: { jdouble a = get_double(sp - 4), b = get_double(sp - 2); sp -= 3; sp[-1].i = a < b ? -1 : a == b ? 0 : 1; } NEXT(1);

op_ifeq: ia = (--sp)->i; BRANCH(ia == 0, s16(pc + 1));
op_ifne: ia = (--sp)->i; BRANCH(ia != 0, s16(pc + 1));
op_iflt: ia = (--sp)->i; BRANCH(ia < 0, s16(pc + 1));
op_ifge: ia = (--sp)->i; BRANCH(ia >= 0, s16(pc + 1));
op_ifgt: ia = (--sp)->i; BRANCH(ia > 0, s16(pc + 1));
op_ifle: ia = (--sp)->i; BRANCH(ia <= 0, s16(pc + 1));
op_if_icmpeq: sp -= 2; BRANCH(sp[0].u == sp[1].u, s16(pc + 1));
op_if_icmpne: sp -= 2; BRANCH(sp[0].u != sp[1].u, s16(pc + 1));
op_if_icmplt: sp -= 2; BRANCH(sp[0].i < sp[1].i, s16(pc + 1));
op_if_icmpge: sp -= 2; BRANCH(sp[0].i >= sp[1].i, s16(pc + 1));
op_if_icmpgt: sp -= 2; BRANCH(sp[0].i > sp[1].i, s16(pc + 1));
op_if_icmple: sp -= 2; BRANCH(sp[0].i <= sp[1].i, s16(pc + 1));
op_ifnull: o = (--sp)->l; BRANCH(o == 0, s16(pc + 1));
op_ifnonnull: o = (--sp)->l; BRANCH(o != 0, s16(pc + 1));
op_goto: BRANCH(1, s16(pc + 1));
op_goto_w: ia = s32(pc + 1); if (ia <= 0) TICK(); pc += ia; DISPATCH();
op_jsr: (sp++)->i = (jint)(pc + 3 - m->code); pc += s16(pc + 1); DISPATCH();
op_jsr_w: (sp++)->i = (jint)(pc + 5 - m->code); pc += s32(pc + 1); DISPATCH();
op_ret: pc = m->code + locals[pc[1]].i; DISPATCH();

op_tableswitch: {
    uint8_t *p = m->code + (((pc - m->code) + 4) & ~3);
    jint def = s32(p), low = s32(p + 4), high = s32(p + 8), key = (--sp)->i;
    jint offset = key < low || key > high ? def : s32(p + 12 + (key - low) * 4);
    if (offset <= 0) TICK();
    pc += offset;
    DISPATCH();
}
op_lookupswitch: {
    uint8_t *p = m->code + (((pc - m->code) + 4) & ~3);
    jint def = s32(p), n = s32(p + 4), key = (--sp)->i, offset = def;
    jint lo = 0, hi = n - 1;
    while (lo <= hi) {
        jint mid = (lo + hi) >> 1, k = s32(p + 8 + mid * 8);
        if (k == key) { offset = s32(p + 12 + mid * 8); break; }
        if (k < key) lo = mid + 1; else hi = mid - 1;
    }
    if (offset <= 0) TICK();
    pc += offset;
    DISPATCH();
}

op_ireturn: ia = 1; goto do_return;
op_lreturn: ia = 2; goto do_return;
op_return: ia = 0;
do_return: {
    Slot *dest = fp->locals;
    if (ia == 1) dest[0] = sp[-1];
    else if (ia == 2) { Slot a = sp[-2], b = sp[-1]; dest[0] = a; dest[1] = b; }
    if (fp->lock) { SAVE(); monitor_exit(t, fp->lock); }
    if (fp->entry) { t->fp = fp - 1; return; }
    t->fp = fp - 1;
    LOAD_FRAME();
    sp = dest + ia;
    NEXT(insn_length_invoke(*pc));
}

op_getstatic: {
    Field *f = cp->resolved[u16(pc + 1)];
    if (!f) { SAVE(); f = resolve_field(t, cls, u16(pc + 1), 1); CHECK(); }
    if (f->owner->state != CS_READY) { SAVE(); init_class(t, f->owner); CHECK(); }
    uint8_t *p = f->owner->statics + f->offset;
    switch (f->kind) {
    case K_BYTE: case K_BOOL: (sp++)->i = *(int8_t *)p; break;
    case K_CHAR: (sp++)->i = *(uint16_t *)p; break;
    case K_SHORT: (sp++)->i = *(int16_t *)p; break;
    case K_WIDE: __builtin_memcpy(sp, p, 8); sp += 2; break;
    default: (sp++)->u = *(uint32_t *)p;
    }
    NEXT(3);
}
op_putstatic: {
    Field *f = cp->resolved[u16(pc + 1)];
    if (!f) { SAVE(); f = resolve_field(t, cls, u16(pc + 1), 1); CHECK(); }
    if (f->owner->state != CS_READY) { SAVE(); init_class(t, f->owner); CHECK(); }
    uint8_t *p = f->owner->statics + f->offset;
    switch (f->kind) {
    case K_BYTE: *(int8_t *)p = (int8_t)(--sp)->i; break;
    case K_BOOL: *(int8_t *)p = (int8_t)((--sp)->i & 1); break;
    case K_CHAR: case K_SHORT: *(uint16_t *)p = (uint16_t)(--sp)->i; break;
    case K_WIDE: sp -= 2; __builtin_memcpy(p, sp, 8); break;
    default: *(uint32_t *)p = (--sp)->u;
    }
    NEXT(3);
}
op_getfield: {
    Field *f = cp->resolved[u16(pc + 1)];
    if (!f) { SAVE(); f = resolve_field(t, cls, u16(pc + 1), 0); CHECK(); }
    o = sp[-1].l;
    if (!o) NPE();
    uint8_t *p = (uint8_t *)o + f->offset;
    switch (f->kind) {
    case K_BYTE: case K_BOOL: sp[-1].i = *(int8_t *)p; break;
    case K_CHAR: sp[-1].i = *(uint16_t *)p; break;
    case K_SHORT: sp[-1].i = *(int16_t *)p; break;
    case K_WIDE: __builtin_memcpy(sp - 1, p, 8); sp++; break;
    default: sp[-1].u = *(uint32_t *)p;
    }
    NEXT(3);
}
op_putfield: {
    Field *f = cp->resolved[u16(pc + 1)];
    if (!f) { SAVE(); f = resolve_field(t, cls, u16(pc + 1), 0); CHECK(); }
    if (f->kind == K_WIDE) {
        o = sp[-3].l;
        if (!o) NPE();
        __builtin_memcpy((uint8_t *)o + f->offset, sp - 2, 8);
        sp -= 3;
    } else {
        o = sp[-2].l;
        if (!o) NPE();
        uint8_t *p = (uint8_t *)o + f->offset;
        switch (f->kind) {
        case K_BYTE: *(int8_t *)p = (int8_t)sp[-1].i; break;
        case K_BOOL: *(int8_t *)p = (int8_t)(sp[-1].i & 1); break;
        case K_CHAR: case K_SHORT: *(uint16_t *)p = (uint16_t)sp[-1].i; break;
        default: *(uint32_t *)p = sp[-1].u;
        }
        sp -= 2;
    }
    NEXT(3);
}

op_invokevirtual: {
    uint16_t index = u16(pc + 1);
    Method *rm = cp->resolved[index];
    if (!rm) { SAVE(); rm = resolve_method(t, cls, index); CHECK(); }
    o = sp[-rm->arg_slots].l;
    if (!o) NPE();
    if (rm->vtable_index >= 0 && !(rm->owner->access & ACC_INTERFACE)) target = o->cls->vtable[rm->vtable_index];
    else if (rm->access & ACC_PRIVATE) target = rm;
    else goto dynamic_lookup;
    goto do_invoke;
}
op_invokeinterface: {
    uint16_t index = u16(pc + 1);
    Method *rm = cp->resolved[index];
    if (!rm) { SAVE(); rm = resolve_method(t, cls, index); CHECK(); }
    o = sp[-rm->arg_slots].l;
    if (!o) NPE();
    if (rm->vtable_index >= 0 && !(rm->owner->access & ACC_INTERFACE)) { target = o->cls->vtable[rm->vtable_index]; goto do_invoke; }
    goto dynamic_lookup;
}
dynamic_lookup: {
    uint16_t index = u16(pc + 1);
    Method *rm = cp->resolved[index];
    InlineCache *ic = &cp->cache[index];
    if (ic->cls == o->cls) target = ic->target;
    else {
        target = find_virtual(o->cls, rm);
        if (!target) {
            char text[600], name[256];
            snprintf(text, sizeof(text), "%s.%s%s", class_java_name(o->cls, name, sizeof(name)), rm->name, rm->desc);
            THROW("java/lang/AbstractMethodError", text);
        }
        ic->cls = o->cls;
        ic->target = target;
    }
    goto do_invoke;
}
op_invokespecial: {
    uint16_t index = u16(pc + 1);
    Method *rm = cp->resolved[index];
    if (!rm) { SAVE(); rm = resolve_method(t, cls, index); CHECK(); }
    if (!sp[-rm->arg_slots].l) NPE();
    target = rm;
    if (rm->name[0] != '<' && !(rm->access & ACC_PRIVATE) && (cls->access & ACC_SUPER) && rm->owner != cls &&
        cls->super && !(rm->owner->access & ACC_INTERFACE) && is_assignable(cls, rm->owner)) {
        Method *s = find_virtual(cls->super, rm);
        if (s) target = s;
    }
    goto do_invoke;
}
op_invokestatic: {
    uint16_t index = u16(pc + 1);
    target = cp->resolved[index];
    if (!target) { SAVE(); target = resolve_method(t, cls, index); CHECK(); }
    if (target->owner->state != CS_READY) { SAVE(); init_class(t, target->owner); CHECK(); }
    goto do_invoke;
}
do_invoke: {
    Slot *args = sp - target->arg_slots;
    if (target->access & ACC_NATIVE) {
        SAVE();
        if (!target->bound || !target->native) { bind_native(t, target); CHECK(); }
        Object *lock = 0;
        if (target->access & ACC_SYNCHRONIZED) {
            lock = target->access & ACC_STATIC ? class_mirror(t, target->owner) : args[0].l;
            monitor_enter(t, lock);
        }
        target->native(t, args);
        if (lock) monitor_exit(t, lock);
        CHECK();
        sp = args + ret_slots(target->ret);
        NEXT(insn_length_invoke(*pc));
    }
    if (!target->code) {
        char text[600], name[256];
        snprintf(text, sizeof(text), "%s.%s%s", class_java_name(target->owner, name, sizeof(name)), target->name, target->desc);
        THROW("java/lang/AbstractMethodError", text);
    }
    if (fp + 1 >= t->frames_end || args + target->max_locals + target->max_stack > t->stack_end)
        THROW("java/lang/StackOverflowError", 0);
    fp->pc = pc;
    fp->sp = args;
    if (vm_trace_method) vm_trace_call(t, target);
    Frame *nf = fp + 1;
    nf->method = target;
    nf->locals = args;
    nf->base = args + target->max_locals;
    nf->sp = nf->base;
    nf->pc = target->code;
    nf->cp = &target->owner->cp;
    nf->entry = 0;
    nf->lock = 0;
    t->fp = nf;
    LOAD_FRAME();
    if (m->access & ACC_SYNCHRONIZED) {
        fp->lock = m->access & ACC_STATIC ? class_mirror(t, cls) : locals[0].l;
        monitor_enter(t, fp->lock);
    }
    TICK();
    DISPATCH();
}

op_new: {
    SAVE();
    Class *k = cp->resolved[u16(pc + 1)];
    if (!k) { k = resolve_class(t, cls, u16(pc + 1)); CHECK(); }
    if (k->access & (ACC_ABSTRACT | ACC_INTERFACE)) {
        char name[256];
        THROW("java/lang/InstantiationError", class_java_name(k, name, sizeof(name)));
    }
    if (k->state != CS_READY) { init_class(t, k); CHECK(); }
    o = new_object(t, k);
    CHECK();
    (sp++)->l = o;
    NEXT(3);
}
op_newarray:
    SAVE();
    arr = new_prim_array(t, pc[1], sp[-1].i);
    CHECK();
    sp[-1].l = &arr->o;
    NEXT(2);
op_anewarray: {
    SAVE();
    Class *k = resolve_class(t, cls, u16(pc + 1));
    CHECK();
    arr = new_object_array(t, k, sp[-1].i);
    CHECK();
    sp[-1].l = &arr->o;
    NEXT(3);
}
op_multianewarray: {
    SAVE();
    Class *k = resolve_class(t, cls, u16(pc + 1));
    CHECK();
    int dims = pc[3];
    sp -= dims;
    for (int i = 0; i < dims; i++)
        if (sp[i].i < 0) THROW("java/lang/NegativeArraySizeException", 0);
    fp->sp = sp + dims;
    arr = multi_array(t, k, sp, dims);
    CHECK();
    (sp++)->l = &arr->o;
    NEXT(4);
}
op_arraylength:
    arr = (Array *)sp[-1].l;
    if (!arr) NPE();
    sp[-1].i = arr->length;
    NEXT(1);
op_athrow:
    o = sp[-1].l;
    if (!o) NPE();
    SAVE();
    t->exception = o;
    goto exception;
op_checkcast: {
    o = sp[-1].l;
    if (o) {
        Class *k = cp->resolved[u16(pc + 1)];
        if (!k) { SAVE(); k = resolve_class(t, cls, u16(pc + 1)); CHECK(); }
        if (!is_assignable(o->cls, k)) {
            char text[600], a[256], b[256];
            snprintf(text, sizeof(text), "%s cannot be cast to %s", class_java_name(o->cls, a, sizeof(a)), class_java_name(k, b, sizeof(b)));
            THROW("java/lang/ClassCastException", text);
        }
    }
    NEXT(3);
}
op_instanceof: {
    o = sp[-1].l;
    if (o) {
        Class *k = cp->resolved[u16(pc + 1)];
        if (!k) { SAVE(); k = resolve_class(t, cls, u16(pc + 1)); CHECK(); }
        sp[-1].i = is_assignable(o->cls, k);
    } else sp[-1].i = 0;
    NEXT(3);
}
op_monitorenter:
    o = (--sp)->l;
    if (!o) NPE();
    SAVE();
    monitor_enter(t, o);
    CHECK();
    NEXT(1);
op_monitorexit:
    o = (--sp)->l;
    if (!o) NPE();
    SAVE();
    if (monitor_exit(t, o)) THROW("java/lang/IllegalMonitorStateException", 0);
    NEXT(1);
op_wide: {
    uint16_t index = u16(pc + 2);
    switch (pc[1]) {
    case 0x15: case 0x17: case 0x19: (sp++)->u = locals[index].u; NEXT(4);
    case 0x16: case 0x18: sp[0] = locals[index]; sp[1] = locals[index + 1]; sp += 2; NEXT(4);
    case 0x36: case 0x38: case 0x3a: locals[index] = *--sp; NEXT(4);
    case 0x37: case 0x39: sp -= 2; locals[index] = sp[0]; locals[index + 1] = sp[1]; NEXT(4);
    case 0x84: locals[index].i += s16(pc + 4); NEXT(6);
    case 0xa9: pc = m->code + locals[index].i; DISPATCH();
    }
    THROW("java/lang/VerifyError", "bad wide instruction");
}
op_bad: {
    char text[64];
    snprintf(text, sizeof(text), "unsupported bytecode 0x%02x", *pc);
    THROW("java/lang/VerifyError", text);
}

exception: {
    Object *e = t->exception;
    for (;;) {
        uint32_t offset = (uint32_t)(pc - m->code);
        for (uint16_t i = 0; i < m->handler_count; i++) {
            Handler *h = &m->handlers[i];
            if (offset < h->start || offset >= h->end) continue;
            if (h->type) {
                Class *k = cp->resolved[h->type];
                if (!k) {
                    t->exception = 0;
                    k = resolve_class(t, cls, h->type);
                    t->exception = e;
                    if (!k) continue;
                }
                if (!is_assignable(e->cls, k)) continue;
            }
            sp = fp->base;
            (sp++)->l = e;
            t->exception = 0;
            pc = m->code + h->handler;
            DISPATCH();
        }
        if (fp->lock) { t->exception = 0; monitor_exit(t, fp->lock); t->exception = e; }
        if (fp->entry) { t->fp = fp - 1; return; }
        t->fp = fp - 1;
        LOAD_FRAME();
    }
}
}
