/* LWJGL 2 natives: the display is a Barnix OpenGL context on the BGX canvas;
 * keyboard and mouse events come from BGX. LWJGL key codes are DirectInput
 * scan codes, which are exactly the set-1 codes BGX reports (extended keys
 * with bit 7 set), so they pass through unchanged. */
#include "jvm.h"
#include <barnix_opengl.h>
#include "lwjgl_gl.h"

#define N(name) static void name(Thread *t, Slot *a)

void *plat_gl_memory(uint32_t *bytes);
extern const NativeEntry natives_gl[];

static Object *kept[8];
static int kept_ready;
void lwjgl_keep(int slot, Object *buffer)
{
    if (!kept_ready) { for (int i = 0; i < 8; i++) heap_pin(&kept[i]); kept_ready = 1; }
    if (slot >= 0 && slot < 8) kept[slot] = buffer;
}

/* ---- display ---- */
static int opened, width, height;
N(display_open)
{
    (void)t;
    int w = a[0].i, h = a[1].i;
    if (opened) { a[0].i = 0; return; }   /* one context for the program's lifetime */
    int dw = 640, dh = 400;
    bgx_display(&dw, &dh);
    /* Barnix shows one program full screen, and present() shrinks a frame
     * larger than the display: render at the display size, divided by the
     * smallest whole factor that keeps the software rasterizer near 1000
     * pixels wide (present() enlarges it back). */
    (void)w; (void)h;
    int k = (dw + 999) / 1000;
    if (k < 1) k = 1;
    w = dw / k; h = dh / k;
    uint32_t buffers = (uint32_t)w * (uint32_t)h * 8, bytes = 0;
    uint8_t *memory = plat_gl_memory(&bytes);
    if (!memory || bytes < buffers + (4U << 20)) {
        vm_printf(1, "bjvm: not enough memory for a %dx%d OpenGL display (%u KiB reserved)\n", w, h, bytes >> 10);
        a[0].i = -1;
        return;
    }
    unsigned int *color = (unsigned int *)memory;
    float *depth = (float *)(memory + (uint32_t)w * (uint32_t)h * 4);
    if (bxgl_context(color, depth, w, h, memory + buffers, bytes - buffers) || bgx_open(bxgl_canvas(), color, w, h)) { a[0].i = -1; return; }
    opened = 1; width = w; height = h;
    a[0].i = 0;
}
N(display_swap) { (void)t; (void)a; if (opened) bxgl_swap(); }
N(display_close) { (void)t; (void)a; }
N(display_screen_width) { (void)t; int w = 640, h = 400; bgx_display(&w, &h); a[0].i = w; }
N(display_screen_height) { (void)t; int w = 640, h = 400; bgx_display(&w, &h); a[0].i = h; }
N(display_width) { (void)t; a[0].i = width; }
N(display_height) { (void)t; a[0].i = height; }

/* ---- input queues, filled by Display.update ---- */
#define QUEUE 256
static int key_queue[QUEUE * 3], key_count;
static int mouse_queue[QUEUE * 5], mouse_count;
static int mouse_x, mouse_y, mouse_dx, mouse_dy, mouse_wheel, mouse_buttons, close_requested;
static int grabbed;

static void push_key(int key, int down, int ch)
{
    if (key_count == QUEUE) { memmove(key_queue, key_queue + 3, (QUEUE - 1) * 3 * sizeof(int)); key_count--; }
    int *k = key_queue + key_count * 3;
    k[0] = key; k[1] = down; k[2] = ch;
    key_count++;
}
static void push_mouse(int button, int down, int wheel)
{
    if (mouse_count == QUEUE) { memmove(mouse_queue, mouse_queue + 5, (QUEUE - 1) * 5 * sizeof(int)); mouse_count--; }
    int *m = mouse_queue + mouse_count * 5;
    m[0] = button; m[1] = down; m[2] = wheel; m[3] = mouse_x; m[4] = mouse_y;
    mouse_count++;
}

N(display_poll)
{
    (void)t; (void)a;
    BgxEvent e;
    while (bgx_poll(&e)) {
        int down = e.type == BGX_PRESS;
        if (down && e.code == BGX_KEY_BACKSPACE && bgx_key_held(BGX_KEY_CTRL) && bgx_key_held(0x38)) close_requested = 1;
        push_key((int)(e.code & 255), down, down ? (int)(e.key > 0xffff ? 0 : e.key) : 0);
    }
    BgxMouse m;
    bgx_mouse(&m);
    int dw = 640, dh = 400;
    bgx_display(&dw, &dh);
    int x = width ? m.x * width / 640 : m.x, y = height ? m.y * height / 400 : m.y;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= width) x = width - 1;
    if (y >= height) y = height - 1;
    if (grabbed) { mouse_x += m.dx; mouse_y -= m.dy; if (mouse_x < 0) mouse_x = 0; if (mouse_y < 0) mouse_y = 0; if (mouse_x >= width) mouse_x = width - 1; if (mouse_y >= height) mouse_y = height - 1; }
    else { mouse_x = x; mouse_y = height - 1 - y; }
    mouse_dx += m.dx;
    mouse_dy -= m.dy;
    for (int b = 0; b < 3; b++) {
        unsigned int bit = 1U << b;
        if (m.pressed & bit) push_mouse(b, 1, 0);
        if (m.released & bit) push_mouse(b, 0, 0);
    }
    if (m.wheel) { mouse_wheel += m.wheel * 120; push_mouse(-1, 0, m.wheel * 120); }
    mouse_buttons = (int)m.buttons;
}
/* Keyboard.read0(int[] out): drains up to out.length/3 events (key, state, char). */
N(keyboard_read)
{
    (void)t;
    Array *out = (Array *)a[0].l;
    int n = out ? out->length / 3 : 0;
    if (n > key_count) n = key_count;
    memcpy(ADATA(out, jint), key_queue, (size_t)n * 3 * sizeof(int));
    memmove(key_queue, key_queue + n * 3, (size_t)(key_count - n) * 3 * sizeof(int));
    key_count -= n;
    a[0].i = n;
}
N(keyboard_down) { (void)t; a[0].i = bgx_key_held((unsigned int)a[0].i & 255); }
N(keyboard_close) { (void)t; a[0].i = close_requested; }
/* Mouse.read0(int[] out): events (button, state, wheel, x, y). */
N(mouse_read)
{
    (void)t;
    Array *out = (Array *)a[0].l;
    int n = out ? out->length / 5 : 0;
    if (n > mouse_count) n = mouse_count;
    memcpy(ADATA(out, jint), mouse_queue, (size_t)n * 5 * sizeof(int));
    memmove(mouse_queue, mouse_queue + n * 5, (size_t)(mouse_count - n) * 5 * sizeof(int));
    mouse_count -= n;
    a[0].i = n;
}
/* Mouse.state0(int[6]): x, y, dx, dy, wheel, buttons; deltas reset. */
N(mouse_state)
{
    (void)t;
    Array *out = (Array *)a[0].l;
    if (!out || out->length < 6) return;
    jint *o = ADATA(out, jint);
    o[0] = mouse_x; o[1] = mouse_y; o[2] = mouse_dx; o[3] = mouse_dy; o[4] = mouse_wheel; o[5] = mouse_buttons;
    mouse_dx = mouse_dy = mouse_wheel = 0;
}
N(mouse_grab) { (void)t; grabbed = a[0].i; }
N(mouse_set_position) { (void)t; mouse_x = a[0].i; mouse_y = a[1].i; }

/* ---- GLU ---- */
N(glu_perspective) { (void)t; gluPerspective(a[0].f, a[1].f, a[2].f, a[3].f); }
N(glu_look_at) { (void)t; gluLookAt(a[0].f, a[1].f, a[2].f, a[3].f, a[4].f, a[5].f, a[6].f, a[7].f, a[8].f); }
N(glu_ortho2d) { (void)t; gluOrtho2D(a[0].f, a[1].f, a[2].f, a[3].f); }
N(glu_pick_matrix)
{
    GLint *viewport = bufptr(t, a[4].l);
    if (viewport) gluPickMatrix(a[0].f, a[1].f, a[2].f, a[3].f, viewport);
}
N(glu_mipmaps)
{
    void *data = bufptr(t, a[6].l);
    a[0].i = data ? gluBuild2DMipmaps((GLenum)a[0].i, a[1].i, a[2].i, a[3].i, (GLenum)a[4].i, (GLenum)a[5].i, data) : GL_INVALID_VALUE;
}

const NativeEntry natives_lwjgl_own[] = {
    {"org/lwjgl/opengl/Display", "open0", "(II)I", display_open},
    {"org/lwjgl/opengl/Display", "swap0", "()V", display_swap},
    {"org/lwjgl/opengl/Display", "close0", "()V", display_close},
    {"org/lwjgl/opengl/Display", "screenWidth0", "()I", display_screen_width},
    {"org/lwjgl/opengl/Display", "screenHeight0", "()I", display_screen_height},
    {"org/lwjgl/opengl/Display", "width0", "()I", display_width},
    {"org/lwjgl/opengl/Display", "height0", "()I", display_height},
    {"org/lwjgl/opengl/Display", "poll0", "()V", display_poll},
    {"org/lwjgl/input/Keyboard", "read0", "([I)I", keyboard_read},
    {"org/lwjgl/input/Keyboard", "down0", "(I)Z", keyboard_down},
    {"org/lwjgl/input/Keyboard", "close0", "()Z", keyboard_close},
    {"org/lwjgl/input/Mouse", "read0", "([I)I", mouse_read},
    {"org/lwjgl/input/Mouse", "state0", "([I)V", mouse_state},
    {"org/lwjgl/input/Mouse", "grab0", "(Z)V", mouse_grab},
    {"org/lwjgl/input/Mouse", "setPosition0", "(II)V", mouse_set_position},
    {"org/lwjgl/util/glu/GLU", "gluPerspective", "(FFFF)V", glu_perspective},
    {"org/lwjgl/util/glu/GLU", "gluLookAt", "(FFFFFFFFF)V", glu_look_at},
    {"org/lwjgl/util/glu/GLU", "gluOrtho2D", "(FFFF)V", glu_ortho2d},
    {"org/lwjgl/util/glu/GLU", "gluPickMatrix", "(FFFFLjava/nio/IntBuffer;)V", glu_pick_matrix},
    {"org/lwjgl/util/glu/GLU", "gluBuild2DMipmaps", "(IIIIIILjava/nio/ByteBuffer;)I", glu_mipmaps},
    {0, 0, 0, 0}
};
