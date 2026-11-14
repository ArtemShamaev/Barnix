/* Host platform for tests: POSIX files and clocks, malloc. Built with -m32. */
#define _GNU_SOURCE
#include "jvm.h"
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

void *vm_malloc(uint32_t bytes) { return malloc(bytes ? bytes : 1); }
void vm_free(void *p) { free(p); }
void *plat_heap(uint32_t *bytes)
{
    if (!*bytes) *bytes = 256U << 20;
    return malloc(*bytes);
}
static uint64_t now_ns(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec; }
uint64_t plat_millis(void) { return now_ns() / 1000000ULL; }
uint64_t plat_nanos(void) { return now_ns(); }
jlong plat_epoch_ms(void) { struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts); return (jlong)ts.tv_sec * 1000 + ts.tv_nsec / 1000000; }
void plat_idle(uint32_t ms) { usleep(ms * 1000); }
void plat_write(int err, const char *text, uint32_t length) { fwrite(text, 1, length, err ? stderr : stdout); fflush(err ? stderr : stdout); }
int plat_file_size(const char *path) { struct stat s; return stat(path, &s) || S_ISDIR(s.st_mode) ? -1 : (int)s.st_size; }
int plat_file_read(const char *path, uint32_t offset, void *data, uint32_t size)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    int n = (int)pread(fd, data, size, offset);
    close(fd);
    return n;
}
int plat_file_write(const char *path, const void *data, uint32_t size, int append)
{
    int fd = open(path, O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC), 0644);
    if (fd < 0) return -1;
    int ok = write(fd, data, size) == (ssize_t)size;
    close(fd);
    return ok ? 0 : -1;
}
int plat_is_dir(const char *path) { struct stat s; return !stat(path, &s) && S_ISDIR(s.st_mode); }
int plat_list(const char *path, uint32_t *position, char *name, uint32_t capacity)
{
    DIR *d = opendir(path);
    if (!d) return -1;
    struct dirent *e = 0;
    for (uint32_t i = 0; i <= *position; i++) if (!(e = readdir(d))) break;
    if (e) { snprintf(name, capacity, "%s", e->d_name); (*position)++; }
    closedir(d);
    return e ? 1 : 0;
}
int plat_mkdir(const char *path) { return mkdir(path, 0755); }
int plat_delete(const char *path) { return plat_is_dir(path) ? rmdir(path) : unlink(path); }
int plat_rename(const char *from, const char *to) { return rename(from, to); }
int plat_getcwd(char *out, uint32_t capacity) { return getcwd(out, capacity) ? 0 : -1; }
const char *plat_user(void) { const char *u = getenv("USER"); return u ? u : "user"; }
const char *plat_os(void) { return "Linux"; }
void plat_random(void *out, uint32_t length) { for (uint32_t i = 0; i < length; i++) ((uint8_t *)out)[i] = (uint8_t)rand(); }
uint32_t plat_cpus(void) { return 1; }
int plat_poll_key(void) { int c = getchar(); return c == EOF ? -1 : c; }
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
int plat_resolve(const char *host, uint32_t *address)
{
    struct addrinfo hints = {0}, *r;
    hints.ai_family = AF_INET;
    if (getaddrinfo(host, 0, &hints, &r)) return -1;
    *address = ntohl(((struct sockaddr_in *)r->ai_addr)->sin_addr.s_addr);
    freeaddrinfo(r);
    return 0;
}
int plat_net_open(const char *host, uint32_t port)
{
    uint32_t a;
    if (plat_resolve(host, &a)) return -2;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in sa = {0};
    sa.sin_family = AF_INET; sa.sin_port = htons((uint16_t)port); sa.sin_addr.s_addr = htonl(a);
    if (fd < 0 || connect(fd, (struct sockaddr *)&sa, sizeof(sa))) { if (fd >= 0) close(fd); return -1; }
    return fd;
}
int plat_net_send(int h, const void *d, uint32_t n) { return send(h, d, n, 0) == (ssize_t)n ? (int)n : -1; }
int plat_net_recv(int h, void *d, uint32_t n, uint32_t ms)
{
    struct pollfd p = {h, POLLIN, 0};
    int r = poll(&p, 1, (int)ms);
    if (r == 0) return -2;
    if (r < 0) return -1;
    return (int)recv(h, d, n, 0);
}
void plat_net_close(int h) { close(h); }
