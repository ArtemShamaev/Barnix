#include "linux_exec.h"
#include "elf.h"
#include "fs.h"
#include "barnix.h"

extern const unsigned char linux_probe_start[], linux_probe_end[];
static void debug_number(unsigned int value)
{
    char bytes[12]; unsigned int n = 0;
    do { bytes[n++] = '0' + value % 10; value /= 10; } while (value);
    while (n) __asm__ volatile("outb %0, %1" : : "a"(bytes[--n]), "Nd"((unsigned short)0xe9));
    __asm__ volatile("outb %0, %1" : : "a"((unsigned char)'\n'), "Nd"((unsigned short)0xe9));
}
void linux_abi_selftest(int require_big_file, int require_gcc, int require_compile,
                        int require_libc, int require_stdlib_compile)
{
    const char *args[] = {"linux-probe", "argument"};
    int status = -1;
    int failed = 0;
    for (int i = 0; i < 2; i++) {
        if (linux_run_image(linux_probe_start, linux_probe_end - linux_probe_start,
                            2, args, &status) || status != 37) failed = 1;
        debug_number((unsigned int)status);
    }
    const char *fault_args[] = {"linux-probe", "fault"};
    if (linux_run_image(linux_probe_start, linux_probe_end - linux_probe_start,
                        2, fault_args, &status) || status != 139) failed = 1;
    const char *privileged_args[] = {"linux-probe", "privileged"};
    if (linux_run_image(linux_probe_start, linux_probe_end - linux_probe_start,
                        2, privileged_args, &status) || status != 139) failed = 1;
    if (linux_run_image(linux_probe_start, linux_probe_end - linux_probe_start,
                        2, args, &status) || status != 37) failed = 1;
    char contents[10];
    if (fs_size("/linux-probe.dat") != 10 ||
        fs_read("/linux-probe.dat", 0, contents, sizeof(contents)) != 10 ||
        memcmp(contents, "redirected", 10)) failed = 1;
    if (fs_size("/usr/bin/exec-parent.elf") >= 0) {
        if (fs_mkdir("exec-dir") || fs_cd("exec-dir")) failed = 1;
        const char *exec_args[] = {"exec-parent", "/usr/bin/exec-child.elf"};
        if (linux_run("/usr/bin/exec-parent.elf", 2, exec_args, &status) || status != 42) failed = 1;
        char bytes[34];
        if (fs_read("exec-state.dat", 0, bytes, sizeof(bytes)) != 34 || bytes[0] != 'A') failed = 1;
        for (unsigned int i = 1; i < sizeof(bytes); i++) if (bytes[i] != 'B') failed = 1;
        if (fs_cd("/")) failed = 1;
        if (linux_run_image(linux_probe_start, linux_probe_end - linux_probe_start,
                            2, args, &status) || status != 37) failed = 1;
    }
    if (fs_size("/usr/bin/fork-probe.elf") >= 0) {
        const char *fork_args[] = {"fork-probe", "/usr/bin/true", "limits"};
        if (linux_run("/usr/bin/fork-probe.elf", 3, fork_args, &status) || status != 43) failed = 1;
        char bytes[2];
        if (fs_read("fork-state.dat", 0, bytes, 2) != 2 || memcmp(bytes, "AB", 2)) failed = 1;
    }
    if (fs_size("/usr/bin/pipe-probe.elf") >= 0) {
        const char *pipe_args[] = {"/usr/bin/pipe-probe.elf", "/usr/bin/echo", "/usr/bin/cat"};
        if (linux_run("/usr/bin/pipe-probe.elf", 3, pipe_args, &status) || status != 44) failed = 1;
        const char *deadlock_args[] = {"pipe-probe", "deadlock"};
        if (linux_run("/usr/bin/pipe-probe.elf", 2, deadlock_args, &status) || status != 125) failed = 1;
        /* Starting a new launch after a deadlock must reclaim all endpoints. */
        if (linux_run("/usr/bin/pipe-probe.elf", 3, pipe_args, &status) || status != 44) failed = 1;
    }
    if (fs_size("/usr/bin/linux-large-probe.elf") > 640 * 1024) {
        if (linux_run("/usr/bin/linux-large-probe.elf", 2, args, &status) || status != 37)
            failed = 1;
        if (elf_run("/usr/bin/linux-large-probe.elf", 2, args, &status) || status != 37)
            failed = 1;
    }
    if (fs_size("/usr/bin/linux-high-probe.elf") >= 0) {
        int error = linux_run("/usr/bin/linux-high-probe.elf", 2, args, &status);
        debug_number(error < 0 ? (unsigned int)-error : 0);
        debug_number((unsigned int)status);
        if (error || status != 37)
            failed = 1;
    }
    int big_size = fs_size("/usr/bin/linux-20m-probe.elf");
    if (require_big_file && big_size < 20 * 1024 * 1024) failed = 1;
    if (big_size >= 20 * 1024 * 1024) {
        if (linux_run("/usr/bin/linux-20m-probe.elf", 2, args, &status) || status != 37)
            failed = 1;
        if (elf_run("/usr/bin/linux-20m-probe.elf", 2, args, &status) || status != 37)
            failed = 1;
    }
    if (require_gcc) {
        const char *gcc_args[] = {"gcc", "--version"};
        int gcc_error = linux_run("/usr/bin/gcc", 2, gcc_args, &status);
        debug_number(gcc_error < 0 ? (unsigned int)-gcc_error : 0);
        debug_number((unsigned int)status);
        if (gcc_error || status) failed = 1;
    }
    if (require_compile) {
        const char *gcc_args[] = {"/usr/bin/gcc", "-nostdlib", "-static", "-no-pie",
                                  "-Wl,-Ttext-segment=0x80000000", "-o", "/hello", "/hello.c"};
        int gcc_error = linux_run("/usr/bin/gcc", 8, gcc_args, &status);
        debug_number(gcc_error < 0 ? (unsigned int)-gcc_error : 0);
        debug_number((unsigned int)status);
        if (gcc_error || status) failed = 1;
        else {
            const char *hello_args[] = {"/hello"};
            debug_number((unsigned int)fs_size("/hello"));
            int hello_error = linux_run("/hello", 1, hello_args, &status);
            debug_number(hello_error < 0 ? (unsigned int)-hello_error : 0);
            debug_number((unsigned int)status);
            if (hello_error || status != 42) failed = 1;
        }
        const char *gpp_args[] = {"/usr/bin/g++", "-std=c++23", "-fno-exceptions",
                                  "-nostdlib", "-static", "-no-pie",
                                  "-Wl,-Ttext-segment=0x80000000", "-o", "/hello-cpp", "/hello.cpp"};
        int gpp_error = linux_run("/usr/bin/g++", 10, gpp_args, &status);
        debug_number(gpp_error < 0 ? (unsigned int)-gpp_error : 0);
        debug_number((unsigned int)status);
        if (gpp_error || status) failed = 1;
        else {
            const char *cpp_args[] = {"/hello-cpp"};
            int cpp_error = linux_run("/hello-cpp", 1, cpp_args, &status);
            debug_number(cpp_error < 0 ? (unsigned int)-cpp_error : 0);
            debug_number((unsigned int)status);
            if (cpp_error || status != 43) failed = 1;
        }
    }
    if (require_libc) {
        const char *c_args[] = {"/usr/bin/hello-libc.elf"};
        int c_error = linux_run("/usr/bin/hello-libc.elf", 1, c_args, &status);
        debug_number(c_error < 0 ? (unsigned int)-c_error : 0);
        debug_number((unsigned int)status);
        if (c_error || status != 41) failed = 1;
        const char *cpp_args[] = {"/usr/bin/hello-cpp-libc.elf"};
        int cpp_error = linux_run("/usr/bin/hello-cpp-libc.elf", 1, cpp_args, &status);
        debug_number(cpp_error < 0 ? (unsigned int)-cpp_error : 0);
        debug_number((unsigned int)status);
        if (cpp_error || status != 43) failed = 1;
    }
    if (require_stdlib_compile) {
        if (fs_size("/hello-stdlib.c") >= 0) {
            const char *c_compile[] = {"gcc", "-static", "-no-pie",
                "-Wl,-Ttext-segment=0x80000000", "-o", "/hello-stdlib", "/hello-stdlib.c"};
            int error = linux_run("/usr/bin/gcc", 7, c_compile, &status);
            debug_number(error < 0 ? (unsigned int)-error : 0);
            debug_number((unsigned int)status);
            if (error || status) failed = 1;
            else {
                const char *run[] = {"/hello-stdlib"};
                error = linux_run("/hello-stdlib", 1, run, &status);
                debug_number(error < 0 ? (unsigned int)-error : 0);
                debug_number((unsigned int)status);
                if (error || status != 41) failed = 1;
            }
        }
        const char *cpp_compile[] = {"g++", "-std=c++23", "-static", "-no-pie",
            "-Wl,-Ttext-segment=0x80000000", "-o", "/hello-cpp-stdlib", "/hello-cpp-stdlib.cpp"};
        int error = linux_run("/usr/bin/g++", 8, cpp_compile, &status);
        debug_number(error < 0 ? (unsigned int)-error : 0);
        debug_number((unsigned int)status);
        if (error || status) failed = 1;
        else {
            const char *run[] = {"/hello-cpp-stdlib"};
            error = linux_run("/hello-cpp-stdlib", 1, run, &status);
            debug_number(error < 0 ? (unsigned int)-error : 0);
            debug_number((unsigned int)status);
            if (error || status != 43) failed = 1;
        }
    }
    /* QEMU-only boot mode selected explicitly with -append linux-selftest. */
    unsigned int result = failed ? 1 : 0;
    __asm__ volatile("outl %0, %1" : : "a"(result), "Nd"((unsigned short)0xf4));
    for (;;) __asm__ volatile("cli; hlt");
}
