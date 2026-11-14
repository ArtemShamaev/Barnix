/* java: the launcher. java [-cp path] [-Dname=value] [-Xmx<size>] [-verbose]
 *       (class | -jar file.jar) [arguments] */
#include "jvm.h"
#include <stdio.h>
#include <stdlib.h>

extern const uint8_t bjvm_rt[];
extern const uint32_t bjvm_rt_size;
int vm_run(const char *main_class, int argc, const char *const *argv);
char **bjvm_defines;
int bjvm_define_count;

static void usage(void)
{
    vm_print(1, "Usage: java [options] <mainclass> [args...]\n"
                "       java [options] -jar <jarfile> [args...]\n"
                "Options:\n"
                "  -cp, -classpath <path>  directories and jars, separated by ':'\n"
                "  -D<name>=<value>        set a system property\n"
                "  -Xmx<size>              Java heap size (e.g. 256m)\n"
                "  -verbose[:gc|:class]    report collections and class loading\n"
                "  -version                print the version\n");
}

static void add_path_list(const char *list)
{
    char item[512];
    while (*list) {
        uint32_t n = 0;
        while (list[n] && list[n] != ':' && list[n] != ';') n++;
        if (n && n < sizeof(item)) { memcpy(item, list, n); item[n] = 0; classpath_add(item); }
        list += n;
        if (*list) list++;
    }
}

static uint32_t parse_size(const char *s)
{
    uint64_t v = 0;
    while (*s >= '0' && *s <= '9') v = v * 10 + (uint64_t)(*s++ - '0');
    if (*s == 'k' || *s == 'K') v <<= 10;
    else if (*s == 'm' || *s == 'M') v <<= 20;
    else if (*s == 'g' || *s == 'G') v <<= 30;
    return v > 0xf0000000ULL ? 0xf0000000U : (uint32_t)v;
}

/* Main-Class and Class-Path from a jar's manifest. */
static int read_manifest(const char *jar, char *main_class, uint32_t size)
{
    classpath_add(jar);
    uint32_t length;
    uint8_t *m = resource_read("META-INF/MANIFEST.MF", &length);
    if (!m) return -1;
    char dir[512];
    snprintf(dir, sizeof(dir), "%s", jar);
    char *slash = strrchr(dir, '/');
    if (slash) slash[1] = 0; else dir[0] = 0;
    int found = -1;
    char *line = (char *)m;
    while (*line) {
        char *end = line;
        while (*end && *end != '\n' && *end != '\r') end++;
        char saved = *end;
        *end = 0;
        if (!strncmp(line, "Main-Class:", 11)) {
            char *v = line + 11;
            while (*v == ' ') v++;
            snprintf(main_class, size, "%s", v);
            found = 0;
        } else if (!strncmp(line, "Class-Path:", 11)) {
            char *v = line + 11;
            while (*v) {
                while (*v == ' ') v++;
                char *w = v;
                while (*w && *w != ' ') w++;
                char c = *w, full[512];
                *w = 0;
                if (*v) { snprintf(full, sizeof(full), "%s%s", v[0] == '/' ? "" : dir, v); classpath_add(full); }
                *w = c;
                v = w;
            }
        }
        *end = saved;
        line = end;
        while (*line == '\n' || *line == '\r') line++;
    }
    vm_free(m);
    return found;
}

int bjvm_launch(int argc, const char *const *argv)
{
    const char *classpath = 0, *jar = 0, *main_class = 0;
    uint32_t heap_size = 0;
    int i = 1;
    static char *defines[64];
    for (; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] != '-') break;
        if (!strcmp(a, "-cp") || !strcmp(a, "-classpath") || !strcmp(a, "--class-path")) {
            if (++i >= argc) { usage(); return 2; }
            classpath = argv[i];
        } else if (!strcmp(a, "-jar")) {
            if (++i >= argc) { usage(); return 2; }
            jar = argv[i];
            i++;
            break;
        } else if (!strncmp(a, "-D", 2)) {
            if (bjvm_define_count < 64) defines[bjvm_define_count++] = (char *)a + 2;
        } else if (!strncmp(a, "-Xmx", 4)) heap_size = parse_size(a + 4);
        else if (!strncmp(a, "-Xms", 4) || !strncmp(a, "-Xss", 4) || !strncmp(a, "-XX", 3) || !strcmp(a, "-server") || !strcmp(a, "-client")) {}
        else if (!strcmp(a, "-verbose") || !strcmp(a, "-verbose:gc")) vm_verbose = 1;
        else if (!strcmp(a, "-verbose:class")) vm_verbose = 2;
        else if (!strcmp(a, "-version") || !strcmp(a, "--version")) {
            vm_print(1, "java version \"1.8.0\" (Barnix BJVM 0.1)\nBJVM: bytecode interpreter, green threads, x87\n");
            return 0;
        } else if (!strcmp(a, "-h") || !strcmp(a, "-help") || !strcmp(a, "--help") || !strcmp(a, "-?")) { usage(); return 0; }
        else { vm_printf(1, "Unrecognized option: %s\n", a); usage(); return 2; }
    }
    bjvm_defines = defines;
    for (int k = 0; k < bjvm_define_count; k++)
        if (!strncmp(defines[k], "user.home=", 10)) snprintf(vm_home, sizeof(vm_home), "%s", defines[k] + 10);
        else if (!strncmp(defines[k], "bjvm.trace=", 11)) {
            char *dot = strrchr(defines[k], '.');
            if (dot) { *dot = 0; vm_trace_class = defines[k] + 11; vm_trace_method = dot + 1; }
        }
    if (!jar) {
        if (i >= argc) { usage(); return 2; }
        main_class = argv[i++];
    }
    void *heap = plat_heap(&heap_size);
    if (!heap) { vm_print(1, "Error: not enough memory for the Java heap\n"); return 1; }
    heap_init(heap, heap_size);
    classpath_add_memory(bjvm_rt, bjvm_rt_size);
    static char jar_main[256];
    if (jar) {
        if (plat_file_size(jar) < 0) { vm_printf(1, "Error: Unable to access jarfile %s\n", jar); return 1; }
        if (read_manifest(jar, jar_main, sizeof(jar_main))) { vm_printf(1, "no main manifest attribute, in %s\n", jar); return 1; }
        main_class = jar_main;
    }
    if (classpath) add_path_list(classpath);
    else if (!jar) {
        const char *env = 0;
#ifdef BJVM_HOST
        env = getenv("CLASSPATH");
#endif
        add_path_list(env ? env : ".");
    }
    return vm_run(main_class, argc - i, argv + i);
}

int main(int argc, char **argv) { return bjvm_launch(argc, (const char *const *)argv); }
