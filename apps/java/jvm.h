/* Barnix Java Virtual Machine (BJVM): a bytecode interpreter for Java SE
 * class files (versions 45..52), with a non-moving mark-sweep heap and green
 * threads. The class library in apps/java/lib is its own, written for the
 * VM; natives are bound by name in natives_*.c. Built for i386: native
 * Barnix (ring 0) and a 32-bit host build for tests. */
#ifndef BJVM_H
#define BJVM_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>

typedef int8_t jbyte;
typedef uint8_t jboolean;
typedef uint16_t jchar;
typedef int16_t jshort;
typedef int32_t jint;
typedef int64_t jlong;
typedef float jfloat;
typedef double jdouble;

typedef struct Class Class;
typedef struct Thread Thread;
typedef struct Method Method;
typedef struct Field Field;

/* Every heap object starts with this header. monitor is a monitor table
 * index (0 = none). Identity hashes come from the (stable) address. */
typedef struct Object { Class *cls; uint32_t monitor; } Object;
typedef struct Array { Object o; jint length; jint pad; } Array;
#define ADATA(a, T) ((T *)((char *)(a) + sizeof(Array)))
#define FIELD(o, off, T) (*(T *)((char *)(o) + (off)))

typedef union Slot { jint i; jfloat f; Object *l; uint32_t u; } Slot;
static inline jlong get_long(const Slot *s) { jlong v; __builtin_memcpy(&v, s, 8); return v; }
static inline void put_long(Slot *s, jlong v) { __builtin_memcpy(s, &v, 8); }
static inline jdouble get_double(const Slot *s) { jdouble v; __builtin_memcpy(&v, s, 8); return v; }
static inline void put_double(Slot *s, jdouble v) { __builtin_memcpy(s, &v, 8); }

enum {
    ACC_PUBLIC = 1, ACC_PRIVATE = 2, ACC_PROTECTED = 4, ACC_STATIC = 8, ACC_FINAL = 0x10,
    ACC_SYNCHRONIZED = 0x20, ACC_SUPER = 0x20, ACC_VOLATILE = 0x40, ACC_BRIDGE = 0x40,
    ACC_TRANSIENT = 0x80, ACC_VARARGS = 0x80, ACC_NATIVE = 0x100, ACC_INTERFACE = 0x200,
    ACC_ABSTRACT = 0x400, ACC_STRICT = 0x800, ACC_SYNTHETIC = 0x1000, ACC_ANNOTATION = 0x2000,
    ACC_ENUM = 0x4000
};
/* newarray codes, plus T_REF for reference arrays. */
enum { T_BOOLEAN = 4, T_CHAR = 5, T_FLOAT = 6, T_DOUBLE = 7, T_BYTE = 8, T_SHORT = 9,
       T_INT = 10, T_LONG = 11, T_REF = 12 };

/* Storage kinds of fields: how many bytes and how they extend. */
enum { K_INT, K_BYTE, K_CHAR, K_SHORT, K_BOOL, K_WIDE, K_REF, K_FLOAT };

struct Field {
    const char *name, *desc;
    Class *owner;
    uint16_t access, constant;   /* constant: ConstantValue pool index */
    uint8_t kind;
    uint32_t offset;             /* instance: from object start; static: into statics */
};

typedef struct { uint16_t start, end, handler, type; } Handler;
typedef struct { uint16_t pc, line; } LineEntry;
typedef void (*Native)(Thread *t, Slot *args);

struct Method {
    const char *name, *desc;
    Class *owner;
    uint16_t access, max_stack, max_locals, arg_slots;
    char ret;                    /* V Z B C S I J F D L */
    uint8_t *code;
    uint32_t code_length;
    Handler *handlers;
    uint16_t handler_count, line_count;
    LineEntry *lines;
    int vtable_index;            /* -1 for statics, constructors, privates */
    Native native;
    uint16_t exception_count;    /* for reflection (unused) */
    uint8_t bound;               /* native looked up */
    Method *next_overload;       /* unused */
};

enum { CP_UTF8 = 1, CP_INT = 3, CP_FLOAT = 4, CP_LONG = 5, CP_DOUBLE = 6, CP_CLASS = 7,
       CP_STRING = 8, CP_FIELD = 9, CP_METHOD = 10, CP_IMETHOD = 11, CP_NAMETYPE = 12,
       CP_HANDLE = 15, CP_MTYPE = 16, CP_INDY = 18 };
typedef struct { Class *cls; Method *target; } InlineCache;
typedef struct {
    uint16_t count;
    uint8_t *tags;
    uint32_t *data;              /* utf8: char*, numbers, or packed indices */
    void **resolved;             /* Class*, Field*, Method*, String objects */
    InlineCache *cache;          /* per-index receiver cache for calls */
} ConstPool;

enum { CS_LOADED, CS_LINKED, CS_INITIALIZING, CS_READY, CS_ERROR };
enum { CF_ARRAY = 1, CF_PRIMITIVE = 2, CF_REFERENCE_ARRAY = 4, CF_THROWABLE = 8, CF_FINALIZE = 16 };

struct Class {
    const char *name;            /* internal form: java/lang/String, [I */
    Class *super;
    Class **interfaces;          /* direct */
    Class **all_interfaces;      /* transitive, including supers' */
    uint16_t interface_count, all_interface_count;
    uint16_t access, field_count, method_count;
    uint32_t flags;
    int state;
    Thread *init_thread;
    Field *fields;
    Method *methods;
    Method **vtable;
    uint32_t vtable_size;
    uint32_t instance_size;
    uint8_t *statics;
    uint32_t statics_size;
    uint16_t *refs;              /* instance reference offsets (inherited first) */
    uint32_t ref_count;
    uint16_t *static_refs;
    uint32_t static_ref_count;
    ConstPool cp;
    Object *mirror;              /* java.lang.Class */
    Class *component;            /* arrays */
    Class *array_class;          /* the array of this class, once made */
    uint8_t elem_type, elem_size;
    const char *source_file;
    Class *hash_next;
    Class *cast_cache;           /* last class this one was assignable to */
    Object *protection;          /* unused */
    Method *finalizer;
    Class *outer;                /* unused */
    uint32_t id;
};

typedef struct Frame {
    Method *method;
    uint8_t *pc;
    Slot *locals, *sp, *base;    /* base: operand stack start */
    ConstPool *cp;
    Object *lock;                /* synchronized method monitor */
    int entry;                   /* return to C when this frame returns */
} Frame;

enum { TS_NEW, TS_RUNNABLE, TS_SLEEPING, TS_WAITING, TS_BLOCKED, TS_JOINING, TS_DEAD };

struct Thread {
    Object *jthread;
    Object *exception;           /* pending, null when none */
    Slot *stack, *stack_end;
    Frame *frames, *frames_end, *fp;
    int state, id, daemon, interrupted, stop_requested;
    int ticks;
    uint64_t wake;               /* ms deadline for sleeps and timed waits */
    uint32_t wait_monitor, block_monitor;
    int wait_count;              /* recursion count restored after wait */
    int notified;
    void *c_stack;
    uint32_t c_stack_size;
    void *saved_esp;
    void *c_top;                 /* highest address of the C stack */
    Thread *next;
    Method *start_method;
};

/* ---- vm.c ---- */
typedef struct {
    Class *object, *class_, *string, *thread, *throwable, *cloneable, *serializable, *error;
    Class *prims[13];            /* by T_* code; [0] unused */
    Class *prim_arrays[13];
    Class *system, *class_array, *string_array, *object_array, *byte_array, *char_array, *int_array;
    Class *stack_trace_element, *constructor, *method_, *field_;
    uint32_t string_value, string_hash, class_vm, thread_vm, thread_name, thread_daemon,
             thread_priority, thread_alive, throwable_trace, throwable_message, buffer_address,
             buffer_position, buffer_capacity, buffer_limit, buffer_array, buffer_offset,
             ctor_class, ctor_slot, method_class, method_slot, thread_runnable;
    Method *thread_run, *thread_exit, *string_init_chars;
} VM;
extern VM vm;
extern int vm_verbose;
/* -Dbjvm.trace=pkg/Class.method: print the Java stack on every call to it. */
extern const char *vm_trace_class, *vm_trace_method;
void vm_trace_call(Thread *t, Method *m);
extern char vm_home[256];

void fatal(const char *message, const char *detail);
void vm_print(int err, const char *text);
void vm_printf(int err, const char *format, ...);

/* ---- memory ---- */
void *meta_alloc(uint32_t bytes);         /* zeroed, never freed */
char *meta_strdup(const char *s);
const char *symbol(const char *s, uint32_t length);   /* interned C strings */

/* ---- heap.c ---- */
void heap_init(void *base, uint32_t bytes);
Object *heap_alloc(Thread *t, uint32_t bytes);
int heap_is_object(const void *p);
void heap_collect(Thread *t);
uint32_t heap_free_bytes(void), heap_total_bytes(void);
void heap_pin(Object **root);             /* a C global holding a reference */
Object *new_object(Thread *t, Class *c);
Array *new_array(Thread *t, Class *array_class, jint length);
Array *new_prim_array(Thread *t, int type, jint length);
Array *new_object_array(Thread *t, Class *element, jint length);
Object *clone_object(Thread *t, Object *o);

/* ---- class.c ---- */
void classpath_add(const char *path);
void classpath_add_memory(const uint8_t *zip, uint32_t size);
uint8_t *resource_read(const char *name, uint32_t *size);  /* vm_free the result */
Class *load_class(Thread *t, const char *name);           /* throws NoClassDefFoundError */
Class *find_loaded(const char *name);
Class *array_of(Thread *t, Class *element);
Class *class_for_descriptor(Thread *t, const char *desc);
int init_class(Thread *t, Class *c);                       /* 0 ok, -1 exception */
Method *find_method(Class *c, const char *name, const char *desc);   /* walks supers and interfaces */
Method *find_declared_method(Class *c, const char *name, const char *desc);
Field *find_field(Class *c, const char *name, const char *desc);
Method *find_virtual(Class *c, Method *m);                 /* implementation for a receiver class */
int is_assignable(Class *from, Class *to);
int instance_of(Object *o, Class *c);
Class *resolve_class(Thread *t, Class *from, uint16_t index);
Field *resolve_field(Thread *t, Class *from, uint16_t index, int is_static);
Method *resolve_method(Thread *t, Class *from, uint16_t index);
Object *resolve_string(Thread *t, Class *from, uint16_t index);
uint32_t field_offset(Class *c, const char *name);
Object *class_mirror(Thread *t, Class *c);
Class *mirror_class(Object *mirror);
void class_iterate(void (*fn)(Class *c, void *arg), void *arg);
int count_slots(const char *desc, char *ret);
const char *class_java_name(Class *c, char *buffer, uint32_t size);

/* ---- interp.c ---- */
int invoke(Thread *t, Method *m, Slot *args, Slot *result);  /* 0, or -1 with t->exception */
void interpret(Thread *t);
Thread *current_thread(void);

/* ---- exceptions (vm.c) ---- */
void throw_new(Thread *t, const char *class_name, const char *message);
void throw_object(Thread *t, Object *exception);
void throw_npe(Thread *t);
void print_exception(Thread *t, Object *exception);
Object *make_trace(Thread *t, int skip);

/* ---- strings (vm.c) ---- */
Object *new_string(Thread *t, const char *utf8);
Object *new_string_chars(Thread *t, const jchar *chars, jint length);
Object *intern_string(Thread *t, Object *s);
Object *intern_utf8(Thread *t, const char *utf8);
char *string_utf8(Object *s, char *buffer, uint32_t size);   /* null for null */
jint string_length(Object *s);
jchar *string_chars(Object *s);

/* ---- threads (thread.c) ---- */
extern Thread *thread_current, *thread_list;
Thread *thread_create(Object *jthread, int with_c_stack);
void thread_start(Thread *t, Thread *child);
void thread_yield(Thread *t);
void thread_tick(Thread *t);
void thread_sleep(Thread *t, jlong ms);
void thread_finish(Thread *t);
void threads_run_until_done(Thread *main);
int monitor_enter(Thread *t, Object *o);
int monitor_exit(Thread *t, Object *o);
void monitor_wait(Thread *t, Object *o, jlong ms);
void monitor_notify(Thread *t, Object *o, int all);
int monitor_holds(Thread *t, Object *o);
void monitor_release_object(Object *o);
void threads_scan_roots(void (*mark)(const void *start, const void *end));
uint64_t vm_millis(void);
int thread_others_runnable(Thread *t);

/* ---- natives ---- */
Native native_lookup(Method *m);
void natives_init(void);
typedef struct { const char *cls, *name, *desc; Native fn; } NativeEntry;
extern const NativeEntry natives_core[], natives_io[];
extern int vm_exit_requested, vm_exit_status;
void vm_exit(int status);

/* ---- platform (platform_*.c) ---- */
void *vm_malloc(uint32_t bytes);
void vm_free(void *p);
void *plat_heap(uint32_t *bytes);         /* one big region for the Java heap */
uint64_t plat_millis(void);
uint64_t plat_nanos(void);
void plat_idle(uint32_t ms);
void plat_write(int err, const char *text, uint32_t length);
int plat_file_size(const char *path);      /* -1 when missing */
int plat_file_read(const char *path, uint32_t offset, void *data, uint32_t size);
int plat_file_write(const char *path, const void *data, uint32_t size, int append);
int plat_is_dir(const char *path);
int plat_list(const char *path, uint32_t *position, char *name, uint32_t capacity);  /* 1, 0 end, <0 */
int plat_mkdir(const char *path);
int plat_delete(const char *path);
int plat_rename(const char *from, const char *to);
int plat_getcwd(char *out, uint32_t capacity);
const char *plat_user(void);
const char *plat_os(void);
void plat_random(void *out, uint32_t length);
uint32_t plat_cpus(void);
int plat_poll_key(void);                  /* console key, -1 at end */
/* TCP: handle >= 0, -2 unknown host, -1 failure; recv: bytes, 0 closed, -2 timeout. */
int plat_resolve(const char *host, uint32_t *address);
int plat_net_open(const char *host, uint32_t port);
int plat_net_send(int handle, const void *data, uint32_t length);
int plat_net_recv(int handle, void *data, uint32_t capacity, uint32_t timeout_ms);
void plat_net_close(int handle);
#endif
