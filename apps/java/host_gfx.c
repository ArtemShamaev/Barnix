/* Host stand-ins for the BGX display and input: no window.
 *   BJVM_SHOT=<file.ppm>   every presented frame overwrites that file
 *   BJVM_INPUT=<script>    scripted input, ';'-separated steps
 *                          <frame>:click:<x>,<y>  left click (display pixels)
 *                          <frame>:key:<code>     press and release a set-1 key
 *                          <frame>:hold:<code>:<frames>
 *   BJVM_FPS=1             report the frame count every 5 s on stderr
 *   BJVM_GL_MB=<n>         OpenGL memory in MiB (96) */
#include <barnix_gfx.h>
#include <barnix_opengl.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int frames, display_w = 854, display_h = 480;
static int mouse_x, mouse_y, press_button, release_button;
static int pending_key = -1, release_key = -1, held_key = -1, held_until;
static unsigned int fps_start;
unsigned int bgx_millis(void);

/* Applies the script steps for the frame just presented. */
static void script_step(void)
{
    const char *s = getenv("BJVM_INPUT");
    if (held_key >= 0 && frames >= held_until) { release_key = held_key; held_key = -1; }
    for (; s && *s; ) {
        int frame = atoi(s);
        const char *c = strchr(s, ':');
        if (!c) break;
        c++;
        if (frame == frames) {
            if (!strncmp(c, "click:", 6)) {
                mouse_x = atoi(c + 6) * 640 / display_w;
                const char *y = strchr(c + 6, ',');
                mouse_y = y ? atoi(y + 1) * 400 / display_h : 0;
                press_button = 1;
            } else if (!strncmp(c, "key:", 4)) pending_key = release_key = atoi(c + 4);
            else if (!strncmp(c, "hold:", 5)) {
                held_key = atoi(c + 5);
                const char *n = strchr(c + 5, ':');
                held_until = frames + (n ? atoi(n + 1) : 1);
                pending_key = held_key;
            }
        }
        s = strchr(s, ';');
        if (s) s++;
    }
}
int bgx_open(BgxCanvas *canvas, unsigned int *pixels, int width, int height)
{
    canvas->pixels = pixels; canvas->width = width; canvas->height = height;
    return 0;
}
int bgx_present(const BgxCanvas *c)
{
    const char *shot = getenv("BJVM_SHOT");
    frames++;
    display_w = c->width; display_h = c->height;
    script_step();
    if (getenv("BJVM_FPS")) {
        unsigned int now = bgx_millis();
        if (!fps_start) fps_start = now;
        else if (now - fps_start >= 5000) {
            char line[64];
            int n = 0, v = frames;
            char d[12]; int m = 0;
            do d[m++] = (char)('0' + v % 10); while (v /= 10);
            memcpy(line, "bjvm-host: frame ", 17); n = 17;
            while (m) line[n++] = d[--m];
            memcpy(line + n, " gl-free-kib ", 13); n += 13;
            v = (int)(bxgl_free_memory() >> 10);
            do d[m++] = (char)('0' + v % 10); while (v /= 10);
            while (m) line[n++] = d[--m];
            line[n++] = '\n';
            if (write(2, line, (size_t)n) < 0) {}
            fps_start = now;
        }
    }
    if (!shot) return 0;
    int fd = open(shot, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return 0;
    char header[32];
    int n = 0, v[2] = {c->width, c->height};
    header[n++] = 'P'; header[n++] = '6';
    for (int k = 0; k < 2; k++) {
        char d[12];
        int m = 0, x = v[k];
        do d[m++] = (char)('0' + x % 10); while (x /= 10);
        header[n++] = k ? ' ' : '\n';
        while (m) header[n++] = d[--m];
    }
    memcpy(header + n, "\n255\n", 5);
    n += 5;
    unsigned char *rgb = malloc((size_t)c->width * c->height * 3);
    for (int i = 0; i < c->width * c->height; i++) {
        unsigned int p = c->pixels[i];
        rgb[i * 3] = (unsigned char)(p >> 16); rgb[i * 3 + 1] = (unsigned char)(p >> 8); rgb[i * 3 + 2] = (unsigned char)p;
    }
    if (write(fd, header, (size_t)n) < 0 || write(fd, rgb, (size_t)c->width * c->height * 3) < 0) {}
    free(rgb);
    close(fd);
    return 0;
}
void bgx_close(void) {}
int bgx_display(int *width, int *height) { *width = 854; *height = 480; return 0; }
int bgx_poll(BgxEvent *event)
{
    if (pending_key >= 0) { event->type = BGX_PRESS; event->code = (unsigned int)pending_key; event->key = 0; event->repeat = 0; pending_key = -1; return 1; }
    if (release_key >= 0) { event->type = BGX_RELEASE; event->code = (unsigned int)release_key; event->key = 0; event->repeat = 0; release_key = -1; return 1; }
    return 0;
}
int bgx_key_held(unsigned int code) { return held_key >= 0 && (unsigned int)held_key == code; }
void bgx_mouse(BgxMouse *m)
{
    memset(m, 0, sizeof(*m));
    m->available = 1;
    m->x = mouse_x; m->y = mouse_y;
    if (press_button) { m->pressed = 1; m->buttons = 1; press_button = 0; release_button = 1; }
    else if (release_button) { m->released = 1; release_button = 0; }
}
unsigned int bgx_millis(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned int)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}
unsigned int bgx_cpus(void) { return 1; }
void bgx_parallel(void (*job)(void *argument, unsigned int index), void *argument, unsigned int count)
{
    for (unsigned int i = 0; i < count; i++) job(argument, i);
}
void *plat_gl_memory(uint32_t *bytes)
{
    static void *memory;
    const char *mb = getenv("BJVM_GL_MB");   /* default 96 */
    *bytes = (uint32_t)(mb ? atoi(mb) : 96) << 20;
    if (!memory) memory = malloc(*bytes);
    return memory;
}
/* bxgl_open reads the Barnix API table; BJVM uses bxgl_context instead. */
const BarnixAPI *barnix;
