/* Green threads: every Java thread has its own C stack and Java stack; the
 * interpreter yields at safepoints (calls, backward branches) every few
 * thousand ticks, and on monitors, sleeps and waits. Monitors are inflated
 * on first use into a table indexed by the object header. */
#include "jvm.h"

#define JAVA_STACK_SLOTS (192 * 1024)
#define FRAME_COUNT 6144
#define C_STACK_BYTES (512 * 1024)
#define TICKS 20000

Thread *thread_current, *thread_list;
static Thread *zombie;
static void *boot_esp;
static int thread_ids;

void bjvm_switch(void **save, void *load);
__asm__(".text\n.globl bjvm_switch\n.type bjvm_switch,@function\nbjvm_switch:\n"
        "movl 4(%esp), %eax\nmovl 8(%esp), %edx\n"
        "pushl %ebp\npushl %ebx\npushl %esi\npushl %edi\n"
        "movl %esp, (%eax)\nmovl %edx, %esp\n"
        "popl %edi\npopl %esi\npopl %ebx\npopl %ebp\nret\n");

uint64_t vm_millis(void) { return plat_millis(); }

/* ---- monitors ---- */
typedef struct { Thread *owner; int count; Object *object; uint32_t next_free; } Monitor;
static Monitor *monitors;
static uint32_t monitor_capacity, monitor_free = 0;

static uint32_t monitor_of(Object *o)
{
    if (o->monitor) return o->monitor;
    if (!monitor_free) {
        uint32_t old = monitor_capacity, grown = old ? old * 2 : 1024;
        Monitor *table = vm_malloc(grown * sizeof(Monitor));
        if (!table) fatal("out of memory", "monitor table");
        if (old) memcpy(table, monitors, old * sizeof(Monitor));
        memset(table + old, 0, (grown - old) * sizeof(Monitor));
        vm_free(monitors);
        monitors = table;
        monitor_capacity = grown;
        for (uint32_t i = grown - 1; i >= (old ? old : 1); i--) { monitors[i].next_free = monitor_free; monitor_free = i; }
    }
    uint32_t i = monitor_free;
    monitor_free = monitors[i].next_free;
    memset(&monitors[i], 0, sizeof(Monitor));
    monitors[i].object = o;
    o->monitor = i;
    return i;
}

void monitor_release_object(Object *o)
{
    uint32_t i = o->monitor;
    if (!i || i >= monitor_capacity) return;
    monitors[i].object = 0;
    monitors[i].owner = 0;
    monitors[i].next_free = monitor_free;
    monitor_free = i;
    o->monitor = 0;
}

/* ---- scheduling ---- */
static int ready(Thread *t, uint64_t now)
{
    switch (t->state) {
    case TS_RUNNABLE: return 1;
    case TS_SLEEPING:
        if (t->interrupted || now >= t->wake) { t->state = TS_RUNNABLE; return 1; }
        return 0;
    case TS_WAITING:
        if (t->notified || t->interrupted || (t->wake && now >= t->wake)) {
            t->state = TS_BLOCKED;
            t->block_monitor = t->wait_monitor;
        } else return 0;
        /* fall through */
    case TS_BLOCKED: {
        Monitor *mon = &monitors[t->block_monitor];
        if (mon->owner && mon->owner != t) return 0;
        mon->owner = t;
        mon->count = t->wait_count ? t->wait_count : 1;
        t->wait_count = 0;
        t->state = TS_RUNNABLE;
        return 1;
    }
    }
    return 0;
}

static void switch_to(Thread *from, Thread *to)
{
    if (from == to) return;
    thread_current = to;
    bjvm_switch(&from->saved_esp, to->saved_esp);
    /* Back on this thread. */
    if (zombie && zombie != thread_current) {
        Thread **link = &thread_list;
        while (*link && *link != zombie) link = &(*link)->next;
        if (*link) *link = zombie->next;
        vm_free(zombie->c_stack);
        vm_free(zombie->stack);
        vm_free(zombie->frames);
        vm_free(zombie);
        zombie = 0;
    }
}

/* Pick the next thread after t that can run, t itself last. */
static Thread *pick(Thread *t, int include_self)
{
    for (;;) {
        uint64_t now = plat_millis(), earliest = UINT64_MAX;
        Thread *n = t->next ? t->next : thread_list;
        for (int round = 0; round < 2; round++) {
            for (Thread *k = n; k; k = k->next) {
                if (k == t) { if (round) break; continue; }
                if (k->state != TS_DEAD && k->state != TS_NEW && ready(k, now)) return k;
            }
            n = thread_list;
        }
        if (include_self && t->state != TS_DEAD && ready(t, now)) return t;
        if (!include_self) return 0;
        for (Thread *k = thread_list; k; k = k->next)
            if ((k->state == TS_SLEEPING || (k->state == TS_WAITING && k->wake)) && k->wake < earliest) earliest = k->wake;
        if (earliest == UINT64_MAX) {
            vm_print(1, "bjvm: all threads are blocked (deadlock)\n");
            vm_exit(125);
        }
        plat_idle(earliest > now + 20 ? 20 : (uint32_t)(earliest - now));
    }
}

static void schedule(Thread *t)
{
    Thread *n = pick(t, 1);
    switch_to(t, n);
}

void thread_yield(Thread *t)
{
    Thread *n = pick(t, 0);
    if (n) switch_to(t, n);
}

int thread_others_runnable(Thread *t)
{
    uint64_t now = plat_millis();
    for (Thread *k = thread_list; k; k = k->next) {
        if (k == t || k->state == TS_DEAD || k->state == TS_NEW) continue;
        if (k->state == TS_RUNNABLE) return 1;
        if ((k->state == TS_SLEEPING || (k->state == TS_WAITING && k->wake)) && now >= k->wake) return 1;
        if (k->state == TS_WAITING && (k->notified || k->interrupted)) return 1;
        if (k->state == TS_BLOCKED && !monitors[k->block_monitor].owner) return 1;
    }
    return 0;
}

void thread_tick(Thread *t)
{
    t->ticks = TICKS;
    if (vm_exit_requested) vm_exit(vm_exit_status);
    if (t->stop_requested) {
        t->stop_requested = 0;
        throw_new(t, "java/lang/ThreadDeath", 0);
        return;
    }
    if (thread_others_runnable(t)) thread_yield(t);
}

void thread_sleep(Thread *t, jlong ms)
{
    if (ms <= 0) { thread_yield(t); return; }
    t->state = TS_SLEEPING;
    t->wake = plat_millis() + (uint64_t)ms;
    schedule(t);
    if (t->interrupted) { t->interrupted = 0; throw_new(t, "java/lang/InterruptedException", "sleep interrupted"); }
}

int monitor_enter(Thread *t, Object *o)
{
    uint32_t i = monitor_of(o);
    Monitor *mon = &monitors[i];
    if (!mon->owner) { mon->owner = t; mon->count = 1; return 0; }
    if (mon->owner == t) { mon->count++; return 0; }
    t->state = TS_BLOCKED;
    t->block_monitor = i;
    t->wait_count = 0;
    schedule(t);
    return 0;
}

int monitor_exit(Thread *t, Object *o)
{
    uint32_t i = o->monitor;
    if (!i) return -1;
    Monitor *mon = &monitors[i];
    if (mon->owner != t) return -1;
    if (--mon->count == 0) mon->owner = 0;
    return 0;
}

int monitor_holds(Thread *t, Object *o) { return o->monitor && monitors[o->monitor].owner == t; }

void monitor_wait(Thread *t, Object *o, jlong ms)
{
    uint32_t i = o->monitor;
    if (!i || monitors[i].owner != t) { throw_new(t, "java/lang/IllegalMonitorStateException", "current thread is not owner"); return; }
    if (t->interrupted) { t->interrupted = 0; throw_new(t, "java/lang/InterruptedException", 0); return; }
    t->wait_count = monitors[i].count;
    monitors[i].owner = 0;
    monitors[i].count = 0;
    t->state = TS_WAITING;
    t->wait_monitor = i;
    t->notified = 0;
    t->wake = ms > 0 ? plat_millis() + (uint64_t)ms : 0;
    schedule(t);
    t->notified = 0;
    if (t->interrupted) { t->interrupted = 0; throw_new(t, "java/lang/InterruptedException", 0); }
}

void monitor_notify(Thread *t, Object *o, int all)
{
    uint32_t i = o->monitor;
    if (!i || monitors[i].owner != t) { throw_new(t, "java/lang/IllegalMonitorStateException", "current thread is not owner"); return; }
    for (Thread *k = thread_list; k; k = k->next)
        if (k->state == TS_WAITING && k->wait_monitor == i && !k->notified) {
            k->notified = 1;
            if (!all) return;
        }
}

/* ---- creation and lifetime ---- */
Thread *thread_create(Object *jthread, int with_c_stack)
{
    Thread *t = vm_malloc(sizeof(Thread));
    if (!t) return 0;
    memset(t, 0, sizeof(*t));
    t->stack = vm_malloc(JAVA_STACK_SLOTS * sizeof(Slot));
    t->frames = vm_malloc(FRAME_COUNT * sizeof(Frame));
    if (with_c_stack) { t->c_stack_size = C_STACK_BYTES; t->c_stack = vm_malloc(C_STACK_BYTES); }
    if (!t->stack || !t->frames || (with_c_stack && !t->c_stack)) {
        vm_free(t->stack); vm_free(t->frames); vm_free(t->c_stack); vm_free(t);
        return 0;
    }
    t->stack_end = t->stack + JAVA_STACK_SLOTS;
    t->frames_end = t->frames + FRAME_COUNT;
    memset(t->frames, 0, sizeof(Frame));
    t->fp = t->frames;
    t->jthread = jthread;
    t->state = TS_NEW;
    t->id = ++thread_ids;
    t->ticks = TICKS;
    Thread **link = &thread_list;
    while (*link) link = &(*link)->next;
    *link = t;
    return t;
}

void thread_finish(Thread *t)
{
    Object *j = t->jthread;
    if (j) {
        FIELD(j, vm.thread_alive, jboolean) = 0;
        FIELD(j, vm.thread_vm, Thread *) = 0;
        t->exception = 0;
        monitor_enter(t, j);
        monitor_notify(t, j, 1);
        monitor_exit(t, j);
    }
    t->state = TS_DEAD;
    int live = 0;
    for (Thread *k = thread_list; k; k = k->next) if (k->state != TS_DEAD && !k->daemon && k->state != TS_NEW) live++;
    if (!live) {
        thread_current = 0;
        zombie = t;
        void *ignored;
        bjvm_switch(&ignored, boot_esp);
    }
    zombie = t;
    Thread *n = pick(t, 1);
    thread_current = n;
    void *ignored;
    bjvm_switch(&ignored, n->saved_esp);
    for (;;) {}
}

extern void vm_uncaught(Thread *t, Object *exception);
static void (*main_body)(Thread *t);
void bjvm_set_main_body(void (*body)(Thread *t)) { main_body = body; }

static void thread_trampoline(void)
{
    Thread *t = thread_current;
    if (zombie && zombie != t) {
        Thread **link = &thread_list;
        while (*link && *link != zombie) link = &(*link)->next;
        if (*link) *link = zombie->next;
        vm_free(zombie->c_stack); vm_free(zombie->stack); vm_free(zombie->frames); vm_free(zombie);
        zombie = 0;
    }
    if (main_body) {
        void (*body)(Thread *) = main_body;
        main_body = 0;
        body(t);
        thread_finish(t);
    }
    Method *run = t->start_method;
    if (!run) run = find_virtual(t->jthread->cls, vm.thread_run);
    Slot args[1];
    args[0].l = t->jthread;
    if (run && invoke(t, run, run->arg_slots ? args : 0, 0)) {
        Object *e = t->exception;
        t->exception = 0;
        vm_uncaught(t, e);
    }
    thread_finish(t);
}

static void prepare_stack(Thread *t)
{
    uint32_t *top = (uint32_t *)(((uintptr_t)t->c_stack + t->c_stack_size) & ~(uintptr_t)15);
    t->c_top = top;
    uint32_t *sp = top;
    *--sp = 0;                                   /* trampoline's return address */
    *--sp = (uint32_t)(uintptr_t)thread_trampoline;
    for (int i = 0; i < 4; i++) *--sp = 0;       /* edi esi ebx ebp */
    t->saved_esp = sp;
}

void thread_start(Thread *parent, Thread *child)
{
    (void)parent;
    prepare_stack(child);
    child->state = TS_RUNNABLE;
}

/* Run the main thread; returns when no non-daemon thread is left or on exit. */
void threads_run_until_done(Thread *main)
{
    prepare_stack(main);
    main->state = TS_RUNNABLE;
    thread_current = main;
    bjvm_switch(&boot_esp, main->saved_esp);
    thread_current = 0;
}

void vm_exit(int status)
{
    vm_exit_status = status;
    vm_exit_requested = 1;
    if (!boot_esp) fatal("exit before start", 0);
    void *ignored;
    thread_current = 0;
    bjvm_switch(&ignored, boot_esp);
    for (;;) {}
}

static __attribute__((noinline)) void scan_current(void (*mark)(const void *, const void *), Thread *t)
{
    __builtin_unwind_init();
    volatile uint32_t here = 0;
    mark((const void *)&here, t->c_top);
}

void threads_scan_roots(void (*mark)(const void *start, const void *end))
{
    for (Thread *t = thread_list; t; t = t->next) {
        if (t->state == TS_DEAD) continue;
        mark(&t->jthread, &t->jthread + 1);
        mark(&t->exception, &t->exception + 1);
        Frame *f = t->fp;
        Slot *top = f && f->method ? f->base + f->method->max_stack : t->stack;
        mark(t->stack, top);
        if (t->state == TS_NEW || !t->c_top) continue;
        if (t == thread_current) scan_current(mark, t);
        else mark(t->saved_esp, t->c_top);
    }
}
