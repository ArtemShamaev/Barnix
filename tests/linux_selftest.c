#include "linux_exec.h"
#include "fs.h"
#include "barnix.h"

extern const unsigned char linux_probe_start[], linux_probe_end[];
void linux_abi_selftest(void)
{
    const char *args[] = {"linux-probe", "argument"};
    int status = -1;
    int failed = 0;
    for (int i = 0; i < 2; i++) {
        if (linux_run_image(linux_probe_start, linux_probe_end - linux_probe_start,
                            2, args, &status) || status != 37) failed = 1;
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
    if (fs_size("/bin/exec-parent.elf") >= 0) {
        if (fs_mkdir("exec-dir") || fs_cd("exec-dir")) failed = 1;
        const char *exec_args[] = {"exec-parent", "/bin/exec-child.elf"};
        if (linux_run("/bin/exec-parent.elf", 2, exec_args, &status) || status != 42) failed = 1;
        char bytes[34];
        if (fs_read("exec-state.dat", 0, bytes, sizeof(bytes)) != 34 || bytes[0] != 'A') failed = 1;
        for (unsigned int i = 1; i < sizeof(bytes); i++) if (bytes[i] != 'B') failed = 1;
        if (fs_cd("/")) failed = 1;
        if (linux_run_image(linux_probe_start, linux_probe_end - linux_probe_start,
                            2, args, &status) || status != 37) failed = 1;
    }
    if (fs_size("/bin/fork-probe.elf") >= 0) {
        const char *fork_args[] = {"fork-probe", "/bin/true", "limits"};
        if (linux_run("/bin/fork-probe.elf", 3, fork_args, &status) || status != 43) failed = 1;
        char bytes[2];
        if (fs_read("fork-state.dat", 0, bytes, 2) != 2 || memcmp(bytes, "AB", 2)) failed = 1;
    }
    if (fs_size("/bin/pipe-probe.elf") >= 0) {
        const char *pipe_args[] = {"/bin/pipe-probe.elf", "/bin/echo", "/bin/cat"};
        if (linux_run("/bin/pipe-probe.elf", 3, pipe_args, &status) || status != 44) failed = 1;
        const char *deadlock_args[] = {"pipe-probe", "deadlock"};
        if (linux_run("/bin/pipe-probe.elf", 2, deadlock_args, &status) || status != 125) failed = 1;
        /* Starting a new launch after a deadlock must reclaim all endpoints. */
        if (linux_run("/bin/pipe-probe.elf", 3, pipe_args, &status) || status != 44) failed = 1;
    }
    /* QEMU-only boot mode selected explicitly with -append linux-selftest. */
    unsigned int result = failed ? 1 : 0;
    __asm__ volatile("outl %0, %1" : : "a"(result), "Nd"((unsigned short)0xf4));
    for (;;) __asm__ volatile("cli; hlt");
}
