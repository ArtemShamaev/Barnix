/* Gouse Web: the web browser. HTML5 and CSS3 through the engine in web_*.c;
 * pages come over HTTP and HTTPS (BDK Barnix Net), from files, or are
 * built in (about:home). QuickJS JavaScript. Loading blocks the desktop, which
 * still shows the progress (g_flush). */
#include "gouse.h"
#include "web.h"
#include <barnix_net.h>
#include "../../text.h"
#include "web_jar.h"

extern const GApp app_notepad;
#define TOOLBAR_H 32
#define STATUS_H 20
#define SCROLLBAR_W 14
#define HISTORY 48
#define PAGE_MAX WEB_RESOURCE_MAX
#define FONT_PATH "/usr/share/gouse/web.bwf"
#define HOME "about:home"
enum { ID_BACK = 1, ID_FORWARD, ID_RELOAD, ID_HOME, ID_GO, ID_ADDRESS,
       M_OPEN = 20, M_SAVE, M_SOURCE, M_EXIT, M_RELOAD, M_ZOOM_IN, M_ZOOM_OUT, M_ZOOM_RESET, M_BACK, M_FORWARD, M_HOME,
       M_FIND, M_FIND_NEXT, M_DESKTOP, M_ABOUT, M_IMAGES, M_COOKIES, M_PASSWORDS, M_COPY_ADDRESS, M_COPY_TEXT, M_PASTE_GO, M_DOCS,
       P_OPEN = 60, P_SAVE, P_FIND, P_DOWNLOAD, C_SAVE_PASSWORD, C_CLEAR_COOKIES, C_CLEAR_PASSWORDS };
typedef struct {
    GEdit address;
    int address_focus, address_fresh;   /* fresh: the next key replaces the whole address */
    int failed;
    char status[300], hover[300];
    char history[HISTORY][512];
    int history_scroll[HISTORY], history_count, history_at;
    int scroll, zoom, images;
    WDoc *doc;
    char pending[1024], pending_body[2048];
    int pending_post, pending_history, pending_delay, relayout_at;
    const char *source; int source_length;   /* the page as it came */
    char find[128];
    int dragging_bar;
    int loading, stop, needs_script;
    /* a login form just sent: offered for saving once the next page is in */
    char login_url[512], login_user_field[64], login_user[128], login_password_field[64], login_password[128];
    int login_pending;
} State;
#define S ((State *)w->state)

/* ---------------------------------------------------------------- memory, fonts, a big stack */
static unsigned char *arena, *font_memory, *stack_memory, *jar_memory;
static char *jar_text;   /* the cookie and password files as text */
#define JAR_TEXT (256 * 1024)
static unsigned int arena_size;
static int font_loaded;
#define STACK_SIZE (1u << 20)
#define FONT_MEMORY (3u << 20)
static int memory_ready(void)
{
    if (arena) return 1;
    extern int bw_heap_init(void);
    if (!bw_heap_init()) return 0;
    unsigned int free = g_memory_free();
    if (free < (24u << 20)) return 0;
    font_memory = g_alloc(FONT_MEMORY);
    stack_memory = g_alloc(STACK_SIZE);
    jar_memory = g_alloc(jar_bytes());
    jar_text = g_alloc(JAR_TEXT);
    if (jar_memory) jar_init(jar_memory, jar_bytes());
    /* half of what is left, so other windows (screen-sized) still open */
    arena_size = g_memory_free() / 2;
    if (arena_size > (128u << 20)) arena_size = 128u << 20;
    if (arena_size < (16u << 20)) arena_size = g_memory_free() - (2u << 20);
    arena = g_alloc(arena_size);
    return arena && font_memory && stack_memory && jar_memory;
}
/* The system font as a stand-in face when the font file is missing. */
static void system_font(void)
{
    static const unsigned int ranges[][2] = {{0x20, 0x7e}, {0xa0, 0xff}, {0x401, 0x45f}, {0x2013, 0x2014}, {0x2018, 0x201e}, {0x2022, 0x2022}, {0x2026, 0x2026}};
    unsigned char *out = arena;
    int count = 0;
    for (unsigned int r = 0; r < sizeof(ranges) / sizeof(ranges[0]); r++) count += (int)(ranges[r][1] - ranges[r][0] + 1);
    unsigned char *p = out;
    *p++ = 'B'; *p++ = 'W'; *p++ = 'F'; *p++ = '1'; *p++ = 1; *p++ = 0;
    for (int i = 0; i < 12; i++) *p++ = (unsigned char)"system"[i < 6 ? i : 5] * (i < 6);
    *p++ = 16; *p++ = 0; *p++ = 12; *p++ = 0; *p++ = 4; *p++ = 0;
    *p++ = (unsigned char)count; *p++ = (unsigned char)(count >> 8);
    unsigned int bitmap_bytes = (unsigned)count * 64;
    *p++ = (unsigned char)bitmap_bytes; *p++ = (unsigned char)(bitmap_bytes >> 8); *p++ = (unsigned char)(bitmap_bytes >> 16); *p++ = 0;
    unsigned char *glyphs = p, *bitmaps = p + count * 16;
    int k = 0;
    for (unsigned int r = 0; r < sizeof(ranges) / sizeof(ranges[0]); r++)
        for (unsigned int code = ranges[r][0]; code <= ranges[r][1]; code++, k++) {
            unsigned char *g = glyphs + k * 16;
            unsigned int offset = (unsigned)k * 64;
            g[0] = (unsigned char)code; g[1] = (unsigned char)(code >> 8); g[2] = 0; g[3] = 0;
            g[4] = 128; g[5] = 0; g[6] = 0; g[7] = 12; g[8] = 8; g[9] = 16; g[10] = g[11] = 0;
            g[12] = (unsigned char)offset; g[13] = (unsigned char)(offset >> 8); g[14] = (unsigned char)(offset >> 16); g[15] = 0;
            unsigned char glyph = text_glyph(code);
            for (int row = 0; row < 16; row++) {
                unsigned int bits = barnix->glyph(glyph, (unsigned)row);
                for (int x = 0; x < 8; x += 2)
                    bitmaps[offset + row * 4 + x / 2] = (unsigned char)((bits & (128u >> x) ? 0xf0 : 0) | (bits & (64u >> x) ? 0x0f : 0));
            }
        }
    web_font_init(out, (unsigned)(bitmaps + bitmap_bytes - out), font_memory, FONT_MEMORY);
}
static void fonts(void)
{
    if (font_loaded) return;
    font_loaded = 1;
    int size = barnix->size(FONT_PATH);
    if (size > 0 && size < (int)(arena_size / 2) && barnix->read(FONT_PATH, 0, arena, (unsigned)size) == size &&
        !web_font_init(arena, (unsigned)size, font_memory, FONT_MEMORY)) return;
    system_font();
}
/* Runs fn on the 1 MiB stack: page layout recurses deeply. */
static void on_big_stack(void (*fn)(void *), void *argument)
{
    void *top = stack_memory + STACK_SIZE - 16;
    __asm__ volatile(
        "movl %%esp, %%ebx\n\t"
        "movl %%eax, %%esp\n\t"
        "pushl %%ebx\n\t"
        "pushl %%edi\n\t"
        "call *%%esi\n\t"
        "addl $4, %%esp\n\t"
        "popl %%esp\n\t"
        : "+a"(top) : "S"(fn), "D"(argument) : "ebx", "ecx", "edx", "memory", "cc");
}
typedef struct { WDoc *doc; int width, height; } LayoutJob;
static void layout_job(void *argument) { LayoutJob *j = argument; web_layout(j->doc, j->width, j->height); }

/* ---------------------------------------------------------------- cookies and passwords on disk */
static unsigned int now_seconds(void)
{
    GTime t;
    g_time(&t);
    return jar_epoch(t.year, t.month, t.day, t.hour, t.minute, t.second);
}
static void jar_path(char *out, int capacity, const char *name)
{
    char dir[200];
    g_path_join(dir, sizeof(dir), g_home(), ".gouse-web");
    g_path_join(out, capacity, dir, name);
}
static void jar_read(void)
{
    static int done;
    if (done || !jar_memory) return;
    done = 1;
    char path[256];
    char *text = jar_text;
    if (!text) return;
    jar_path(path, sizeof(path), "cookies");
    int n = g_read_file(path, text, JAR_TEXT - 1);
    if (n > 0) { text[n] = 0; jar_load_cookies(text, now_seconds()); }
    jar_path(path, sizeof(path), "passwords");
    n = g_read_file(path, text, JAR_TEXT - 1);
    if (n > 0) { text[n] = 0; jar_load_passwords(text); }
}
static void jar_write(void)
{
    char *text = jar_text;
    char path[256], dir[200];
    if (!jar_memory || !text || (!jar_cookies_dirty() && !jar_passwords_dirty())) return;
    g_path_join(dir, sizeof(dir), g_home(), ".gouse-web");
    unsigned int cursor = 0; BarnixDirEntry entry;
    if (barnix->directory(dir, &cursor, &entry) < 0) barnix->mkdir(dir);
    if (jar_cookies_dirty()) { int n = jar_save_cookies(text, JAR_TEXT, now_seconds()); jar_path(path, sizeof(path), "cookies"); g_write_file(path, text, n); }
    if (jar_passwords_dirty()) { int n = jar_save_passwords(text, JAR_TEXT); jar_path(path, sizeof(path), "passwords"); g_write_file(path, text, n); }
}
static const char *cookie_headers(void *context, const char *url)
{
    static char header[4096];
    (void)context;
    jar_cookie_header(url, header, sizeof(header), now_seconds());
    return header;
}
static void response_header(void *context, const char *url, const char *line)
{
    (void)context;
    if (w_starts_ci(line, "set-cookie:")) {
        const char *v = line + 11;
        while (*v == ' ') v++;
        jar_set_cookie(url, v, now_seconds());
    }
}

/* ---------------------------------------------------------------- the engine's platform */
static GWindow *loading_window;
void web_progress(const char *text)
{
    if (!loading_window) return;
    char *status = ((State *)loading_window->state)->status;
    if (w_starts_ci(text, "Loading image ")) {
        int n = 0;
        for (const char *p = text + 14; *p >= '0' && *p <= '9'; p++) n = n * 10 + *p - '0';
        g_format(status, 300, "Loading image %d...", n);
    } else g_copy(status, g_tr(text), 300);
    g_invalidate(loading_window);
    g_flush();
}
/* While the network waits: the desktop keeps working; Stop ends the load. */
static int idle(void)
{
    if (!loading_window) return 0;
    g_pump();
    return ((State *)loading_window->state)->stop;
}
unsigned int web_time(void) { return g_now(); }
/* Long script work during a load (the page is not shown yet): a desktop
 * frame now and then, and Stop ends the scripts as well. Never during ticks:
 * the page is on screen and its layout may be gone. */
int web_idle(void)
{
    static unsigned int last;
    if (!loading_window) return 0;
    if (g_now() - last < 40) return ((State *)loading_window->state)->stop;
    last = g_now();
    return idle();
}
long web_inflate(const void *in, unsigned long in_length, void *out, unsigned long capacity, int zlib)
{
    unsigned long used;
    return bnet_inflate(in, in_length, &used, out, capacity, zlib);
}
typedef struct { char *buffer; int length, capacity; unsigned int last_flush; } Sink;
static int sink(void *context, const void *data, unsigned int length)
{
    Sink *k = context;
    const char *p = data;
    if (length > (unsigned)(k->capacity - k->length)) return 1;
    for (unsigned int i = 0; i < length; i++) k->buffer[k->length + (int)i] = p[i];
    k->length += (int)length;
    if (loading_window && ((State *)loading_window->state)->stop) return 1;
    if (g_now() - k->last_flush > 300 && loading_window) {
        char text[64];
        g_format(text, sizeof(text), "%d KB received", k->length / 1024);
        web_progress(text);
        k->last_flush = g_now();
    }
    return 0;
}
int web_fetch(const char *url, WebResource *out, int max_bytes, const char *post_body, const char *post_type)
{
    for (unsigned int i = 0; i < sizeof(*out); i++) ((char *)out)[i] = 0;
    g_copy(out->url, url, sizeof(out->url));
    const char *path = 0;
    if (w_starts_ci(url, "file://")) path = url + 7;
    else if (url[0] == '/') path = url;
    if (path) {
        char clean[512];
        int n = 0;
        while (path[n] && path[n] != '?' && path[n] != '#' && n < 511) { clean[n] = path[n]; n++; }
        clean[n] = 0;
        int size = barnix->size(clean);
        if (size < 0) { g_copy(out->error, "file not found", sizeof(out->error)); return -1; }
        if (size > max_bytes) { g_copy(out->error, "Resource exceeds 16 MiB limit", sizeof(out->error)); return -1; }
        out->data = web_alloc_big((unsigned)size + 1);
        if (!out->data || (size && barnix->read(clean, 0, out->data, (unsigned)size) != size)) { g_copy(out->error, "cannot read the file", sizeof(out->error)); return -1; }
        out->length = size; out->status = 200;
        return 0;
    }
    if (!w_starts_ci(url, "http://") && !w_starts_ci(url, "https://")) { g_copy(out->error, "unsupported address", sizeof(out->error)); return -1; }
    unsigned int start = web_mark();
    Sink k;
    k.buffer = web_take((unsigned)max_bytes + 1);
    if (!k.buffer) { g_copy(out->error, "out of memory", sizeof(out->error)); return -1; }
    k.length = 0; k.capacity = max_bytes; k.last_flush = g_now();
    BnetRequest request;
    for (unsigned int i = 0; i < sizeof(request); i++) ((char *)&request)[i] = 0;
    request.data = sink; request.context = &k;
    request.user_agent = "Mozilla/5.0 (Barnix; i686) GouseWeb/2.0 QuickJS";
    request.request_headers = cookie_headers;
    request.response_header = response_header;
    request.accept = "text/html,application/xhtml+xml,text/css,image/png,*/*;q=0.5";
    request.keep_alive = 1;    /* a page's resources share the connection (load closes it) */
    request.accept_gzip = 1;   /* text arrives 3-5 times smaller */
    if (post_body) { request.method = "POST"; request.body = post_body; request.body_length = (unsigned)g_len(post_body); request.content_type = post_type ? post_type : "application/x-www-form-urlencoded"; }
    int result = bnet_request(url, &request);
    if (g_equal_ci(request.response_encoding, "gzip") && k.length) {
        /* unpacked above the compressed body, then moved down over it */
        long size = bnet_gunzip_size(k.buffer, (unsigned long)k.length);
        char *plain = size >= 0 && size <= max_bytes ? web_take((unsigned)size + 1) : 0;
        long n = plain ? bnet_gunzip(k.buffer, (unsigned long)k.length, plain, (unsigned long)size) : -1;
        if (n < 0) { web_release(start); g_copy(out->error, size > max_bytes ? "Resource exceeds 16 MiB limit" : "damaged gzip data", sizeof(out->error)); return -1; }
        for (long i = 0; i < n; i++) k.buffer[i] = plain[i];
        k.length = (int)n;
    }
    web_release(start + (((unsigned)k.length + 8) & ~7u));
    k.buffer[k.length] = 0;
    out->data = k.buffer; out->length = k.length; out->status = request.status;
    g_copy(out->type, request.response_type, sizeof(out->type));
    if (request.url[0]) g_copy(out->url, request.url, sizeof(out->url));
    if (result && !k.length) { g_copy(out->error, request.error[0] ? request.error : "no answer", sizeof(out->error)); return -1; }
    if (result) g_copy(out->error, request.error, sizeof(out->error));
    return 0;
}

/* ---------------------------------------------------------------- built-in pages */
static const char home_page[] =
    "<!DOCTYPE html><html><head><title>Gouse Web</title><style>"
    "body{font-family:sans-serif;margin:0;background:#eef2f8;color:#1c2430}"
    ".top{background:linear-gradient(to right,#1f4fa8,#4a8ee6);color:#fff;padding:28px 24px 22px}"
    ".top h1{margin:0;font-size:30px}.top p{margin:6px 0 0;opacity:.9}"
    "main{max-width:720px;margin:0 auto;padding:16px}"
    "form{display:flex;gap:8px;margin:0 0 18px}form input[type=text]{flex:1;font-size:16px;padding:6px 10px}"
    "form input[type=submit]{font-size:15px;padding:6px 16px;background:#1f4fa8;color:#fff;border-color:#1f4fa8}"
    ".tiles{display:grid;grid-template-columns:repeat(auto-fill,minmax(200px,1fr));gap:12px}"
    ".tile{display:block;background:#fff;border:1px solid #cdd6e4;border-radius:10px;padding:12px 14px;text-decoration:none;color:#1c2430}"
    ".tile b{display:block;color:#1f4fa8;font-size:16px;margin-bottom:4px}.tile span{font-size:13px;color:#5a6676}"
    "h2{font-size:18px;margin:22px 0 8px}ul{margin:0;padding-left:22px}li{margin:3px 0}code{background:#dde5f2;padding:1px 4px;border-radius:3px}"
    "</style></head><body><div class=top><h1>Gouse Web</h1><p>HTML5, CSS3, JavaScript and Canvas2D</p></div><main>"
    "<form action=\"https://lite.duckduckgo.com/lite/\" method=get><input type=text name=q placeholder=\"Search with DuckDuckGo\"><input type=submit value=Search></form>"
    "<div class=tiles><a class=tile href=\"file:///usr/share/gouse/examples/three-game.html\"><b>Three.js</b><span>CanvasRenderer game / Игра</span></a>"
    "<a class=tile href=\"https://example.com/\"><b>Example Domain</b><span>The simplest page on the web</span></a>"
    "<a class=tile href=\"http://info.cern.ch/hypertext/WWW/TheProject.html\"><b>The first website</b><span>CERN, 1991</span></a>"
    "<a class=tile href=\"https://en.m.wikipedia.org/wiki/Main_Page\"><b>Wikipedia</b><span>The free encyclopedia (mobile)</span></a>"
    "<a class=tile href=\"https://lite.duckduckgo.com/lite/\"><b>DuckDuckGo Lite</b><span>Search without scripts</span></a>"
    "<a class=tile href=\"https://text.npr.org/\"><b>NPR Text</b><span>News, text only</span></a>"
    "<a class=tile href=\"file:///usr/share/doc/barnix/index.html\"><b>Manuals</b><span>C, C++, Nova, Java, HTML/CSS/JS, Barnix API</span></a>"
    "<a class=tile href=\"file:///\"><b>This computer</b><span>Files on the Barnix disk</span></a>"
    "</div><h2>Tips</h2><ul>"
    "<li>Type an address, or words to search for, in the address bar and press Enter.</li>"
    "<li><code>Ctrl+L</code> address bar, <code>F5</code> reload, <code>Backspace</code> back, <code>Ctrl+F</code> find, <code>Ctrl&nbsp;+/-</code> zoom.</li>"
    "<li>View &rarr; Add to desktop makes a shortcut to the page.</li>"
    "<li>PNG pictures are shown; JPEG, GIF, SVG and scripts are not.</li></ul></main></body></html>";
static const char home_page_ru[] =
    "<!DOCTYPE html><html><head><title>Гусь.Веб</title><style>"
    "body{font-family:sans-serif;margin:0;background:#eef2f8;color:#1c2430}"
    ".top{background:linear-gradient(to right,#1f4fa8,#4a8ee6);color:#fff;padding:28px 24px 22px}"
    ".top h1{margin:0;font-size:30px}.top p{margin:6px 0 0;opacity:.9}"
    "main{max-width:720px;margin:0 auto;padding:16px}"
    "form{display:flex;gap:8px;margin:0 0 18px}form input[type=text]{flex:1;font-size:16px;padding:6px 10px}"
    "form input[type=submit]{font-size:15px;padding:6px 16px;background:#1f4fa8;color:#fff;border-color:#1f4fa8}"
    ".tiles{display:grid;grid-template-columns:repeat(auto-fill,minmax(200px,1fr));gap:12px}"
    ".tile{display:block;background:#fff;border:1px solid #cdd6e4;border-radius:10px;padding:12px 14px;text-decoration:none;color:#1c2430}"
    ".tile b{display:block;color:#1f4fa8;font-size:16px;margin-bottom:4px}.tile span{font-size:13px;color:#5a6676}"
    "h2{font-size:18px;margin:22px 0 8px}ul{margin:0;padding-left:22px}li{margin:3px 0}code{background:#dde5f2;padding:1px 4px;border-radius:3px}"
    "</style></head><body><div class=top><h1>Гусь.Веб</h1><p>HTML5, CSS3, JavaScript и Canvas2D</p></div><main>"
    "<form action=\"https://lite.duckduckgo.com/lite/\" method=get><input type=text name=q placeholder=\"Поиск в DuckDuckGo\"><input type=submit value=Найти></form>"
    "<div class=tiles><a class=tile href=\"file:///usr/share/gouse/examples/three-game.html\"><b>Three.js</b><span>CanvasRenderer game / Игра</span></a>"
    "<a class=tile href=\"https://example.com/\"><b>Example Domain</b><span>Самая простая страница в сети</span></a>"
    "<a class=tile href=\"http://info.cern.ch/hypertext/WWW/TheProject.html\"><b>Первый сайт</b><span>CERN, 1991 год</span></a>"
    "<a class=tile href=\"https://ru.m.wikipedia.org/wiki/\"><b>Википедия</b><span>Свободная энциклопедия (мобильная)</span></a>"
    "<a class=tile href=\"https://lite.duckduckgo.com/lite/\"><b>DuckDuckGo Lite</b><span>Поиск без скриптов</span></a>"
    "<a class=tile href=\"https://text.npr.org/\"><b>NPR Text</b><span>Новости, только текст</span></a>"
    "<a class=tile href=\"file:///usr/share/doc/barnix/index.html\"><b>Справочник</b><span>C, C++, Nova, Java, HTML/CSS/JS, Barnix API</span></a>"
    "<a class=tile href=\"file:///\"><b>Этот компьютер</b><span>Файлы на диске Barnix</span></a>"
    "</div><h2>Подсказки</h2><ul>"
    "<li>Введите в адресную строку адрес или слова для поиска и нажмите Enter.</li>"
    "<li><code>Ctrl+L</code> адресная строка, <code>F5</code> обновить, <code>Backspace</code> назад, <code>Ctrl+F</code> поиск, <code>Ctrl&nbsp;+/-</code> масштаб.</li>"
    "<li>Вид &rarr; Ярлык на рабочий стол создаёт ярлык на страницу.</li>"
    "<li>Картинки PNG показываются; JPEG, GIF, SVG и скрипты &mdash; нет.</li></ul></main></body></html>";
static int append_escaped(char *out, int at, int capacity, const char *s, int length)
{
    for (int i = 0; i < length && at < capacity - 8; i++) {
        char c = s[i];
        if (c == '<') { g_copy(out + at, "&lt;", 5); at += 4; }
        else if (c == '&') { g_copy(out + at, "&amp;", 6); at += 5; }
        else if (c == '>') { g_copy(out + at, "&gt;", 5); at += 4; }
        else if (c == '"') { g_copy(out + at, "&quot;", 7); at += 6; }
        else out[at++] = c;
    }
    out[at] = 0;
    return at;
}
/* A folder listing for file: addresses. */
static char *directory_page(const char *path, int *length)
{
    int capacity = 256 * 1024;
    char *out = web_alloc_big((unsigned)capacity);
    if (!out) return 0;
    int n = g_format(out, capacity, "<!DOCTYPE html><html><head><title>%s</title><style>body{font-family:sans-serif;margin:16px}a{text-decoration:none}li{margin:2px 0}</style></head><body><h2>", path);
    n = append_escaped(out, n, capacity, path, g_len(path));
    n += g_format(out + n, capacity - n, "</h2><ul><li><a href=\"..\">..</a></li>");
    unsigned int cursor = 0;
    BarnixDirEntry entry;
    while (barnix->directory(path, &cursor, &entry) > 0 && n < capacity - 1024) {
        if (g_equal(entry.name, ".") || g_equal(entry.name, "..")) continue;
        n += g_format(out + n, capacity - n, "<li><a href=\"");
        n = append_escaped(out, n, capacity, entry.name, g_len(entry.name));
        if (entry.kind == 4) out[n++] = '/';
        n += g_format(out + n, capacity - n, "\">%s", entry.kind == 4 ? "<b>" : "");
        n = append_escaped(out, n, capacity, entry.name, g_len(entry.name));
        n += g_format(out + n, capacity - n, "%s</a></li>", entry.kind == 4 ? "/</b>" : "");
    }
    n += g_format(out + n, capacity - n, "</ul></body></html>");
    *length = n;
    return out;
}
static int looks_binary(const char *data, int length)
{
    for (int i = 0; i < length && i < 1024; i++) if (data[i] == 0) return 1;
    return 0;
}
static int looks_like_html(const char *data, int length, const char *url)
{
    if (g_ends_ci(url, ".txt") || g_ends_ci(url, ".md") || g_ends_ci(url, ".c") || g_ends_ci(url, ".h") || g_ends_ci(url, ".py") ||
        g_ends_ci(url, ".json") || g_ends_ci(url, ".cfg") || g_ends_ci(url, ".css") || g_ends_ci(url, ".js") || g_ends_ci(url, ".sh")) return 0;
    for (int i = 0; i < length && i < 2048; i++) if (data[i] == '<') return 1;
    return 0;
}

/* ---------------------------------------------------------------- the window */
static void page_rect(GWindow *w, int *x, int *y, int *width, int *height)
{
    *x = 0; *y = TOOLBAR_H; *width = w->w - SCROLLBAR_W; *height = w->h - TOOLBAR_H - STATUS_H;
    if (*width < 50) *width = 50;
    if (*height < 20) *height = 20;
}
static int max_scroll(GWindow *w)
{
    int px, py, pw, ph;
    page_rect(w, &px, &py, &pw, &ph);
    int m = S->doc ? S->doc->height - ph : 0;
    return m > 0 ? m : 0;
}
static void clamp_scroll(GWindow *w)
{
    if (S->scroll > max_scroll(w)) S->scroll = max_scroll(w);
    if (S->scroll < 0) S->scroll = 0;
}
static void title(GWindow *w)
{
    char text[300];
    if (S->doc && S->doc->title[0] && !g_equal(S->doc->title, "Gouse Web") && !g_equal(S->doc->title, "Гусь.Веб")) g_format(text, sizeof(text), "%s - Gouse Web", S->doc->title);
    else g_copy(text, g_tr("Gouse Web"), sizeof(text));
    /* the title bar font has no typographic dashes and quotes */
    char plain[300]; int n = 0;
    const unsigned char *p = (const unsigned char *)text, *end = p + g_len(text);
    while (p < end && n < 290) {
        const unsigned char *at = p;
        int c = w_utf8_decode(&p, end);
        if (c == 0x2013 || c == 0x2014 || c == 0x2212) plain[n++] = '-';
        else if (c == 0x201c || c == 0x201d || c == 0x201e || c == 0xab || c == 0xbb) plain[n++] = '"';
        else if (c == 0x2018 || c == 0x2019) plain[n++] = '\'';
        else if (c == 0xa0) plain[n++] = ' ';
        else while (at < p) plain[n++] = (char)*at++;
    }
    plain[n] = 0;
    g_set_title(w, plain);
}
static void relayout(GWindow *w)
{
    if (!S->doc) return;
    int px, py, pw, ph;
    page_rect(w, &px, &py, &pw, &ph);
    S->doc->zoom = S->zoom;
    LayoutJob job = {S->doc, pw, ph};
    on_big_stack(layout_job, &job);
    if (web_out_of_memory) g_copy(S->status, g_tr("The page is too large: part of it is missing"), sizeof(S->status));
    clamp_scroll(w);
}
typedef struct { const char *html; int length; const char *url; WDoc *doc; int images, charset; } LoadJob;
static void parse_job(void *argument)
{
    LoadJob *j = argument;
    j->doc = web_parse(j->html, j->length, j->url);
    j->doc->charset = j->charset;
    web_load_resources(j->doc, j->images);
}
/* Fetches, parses and lays out url; the old page is gone from then on. */
static void load(GWindow *w, const char *url, const char *post)
{
    State *s = S;
    if (!memory_ready()) { g_error(w, "Gouse Web", "Not enough memory for the browser (it needs about 32 MiB)."); return; }
    fonts();
    jar_read();
    loading_window = w;
    s->loading = 1; s->stop = 0; s->needs_script = 0; w->busy = 1;
    bnet_set_idle(idle);
    web_js_destroy(s->doc);
    s->doc = 0; s->source = 0; s->scroll = 0;
    web_arena(arena, arena_size);
    char target[1024];
    g_copy(target, url, sizeof(target));
    if (w_starts_ci(target, "about:docs")) {   /* the manuals: about:docs, about:docs/c */
        const char *page = target[10] == '/' && target[11] ? target + 11 : "index";
        char path[256]; g_format(path, sizeof(path), "file:///usr/share/doc/barnix/%s%s", page, g_ends_ci(page, ".html") ? "" : ".html");
        g_copy(target, path, sizeof(target));
    }
    g_format(s->status, sizeof(s->status), "%s %s", g_tr("Loading"), target);
    g_invalidate(w);
    g_flush();
    const char *html = 0;
    int length = 0, image_page = 0, charset = WCS_UTF8;
    WebResource r;
    for (unsigned int i = 0; i < sizeof(r); i++) ((char *)&r)[i] = 0;
    g_copy(r.url, target, sizeof(r.url));
    s->failed = 0;
    if (g_equal_ci(target, "about:home") || !target[0]) { html = g_russian ? home_page_ru : home_page; length = g_len(html); g_copy(r.url, HOME, sizeof(r.url)); }
    else if (g_equal_ci(target, "about:blank")) { html = "<html><body></body></html>"; length = g_len(html); }
    else {
        const char *path = w_starts_ci(target, "file://") ? target + 7 : target[0] == '/' ? target : 0;
        unsigned int cursor = 0; BarnixDirEntry entry;
        if (path && (path[g_len(path) - 1] == '/' || barnix->directory(path, &cursor, &entry) > 0) && barnix->size(path) < 0) {
            char dir[512];
            g_copy(dir, path, sizeof(dir));
            if (dir[0] && dir[g_len(dir) - 1] != '/') g_append(dir, "/", sizeof(dir));
            html = directory_page(dir, &length);
            g_format(r.url, sizeof(r.url), "file://%s", dir);
        } else if ((s->failed = web_fetch(target, &r, PAGE_MAX, post, 0)) != 0) {
            if (s->stop) g_copy(r.error, g_tr("Loading stopped"), sizeof(r.error));
            char *page = web_alloc(4096);
            int n = g_format(page, 4096, "<html><head><title>%s</title></head><body style=\"font-family:sans-serif;margin:32px;color:#333\"><h2>%s</h2><p>",
                             g_tr("Cannot open the page"), g_tr("Cannot open the page"));
            n = append_escaped(page, n, 4096, target, g_len(target));
            n += g_format(page + n, 4096 - n, "</p><p style=\"color:#a33\">");
            n = append_escaped(page, n, 4096, r.error, g_len(r.error));
            n += g_format(page + n, 4096 - n, "</p><p><a href=\"about:home\">%s</a></p></body></html>", g_tr("Home page"));
            html = page; length = n;
        } else {
            s->source = r.data; s->source_length = r.length;
            int typed_text = w_starts_ci(r.type, "text/plain") || w_starts_ci(r.type, "text/css") || w_starts_ci(r.type, "application/json") ||
                             w_starts_ci(r.type, "text/javascript") || w_starts_ci(r.type, "application/javascript");
            int typed_html = w_starts_ci(r.type, "text/html") || w_starts_ci(r.type, "application/xhtml");
            if (r.length >= 8 && (unsigned char)r.data[0] == 0x89 && r.data[1] == 'P' && r.data[2] == 'N' && r.data[3] == 'G') {
                html = "<html><body style=\"margin:0;background:#2a2a2e;text-align:center\"><img id=picture></body></html>";
                length = g_len(html); image_page = 1;
            } else if (!typed_html && !typed_text && looks_binary(r.data, r.length)) {
                const char *name = target;
                for (const char *p = target; *p; p++) if (*p == '/' && p[1]) name = p + 1;
                char initial[256];
                g_path_join(initial, sizeof(initial), g_home(), name);
                html = "<html><body style=\"font-family:sans-serif;margin:32px\"><h2>Download</h2><p>This file is not a web page.</p></body></html>";
                length = g_len(html);
                g_prompt(w, P_DOWNLOAD, "Save download", "Save the file as:", initial);
            } else if (typed_text || (!typed_html && !looks_like_html(r.data, r.length, target))) {
                int capacity = r.length * 2 + 512;
                char *page = web_alloc_big((unsigned)capacity);
                if (page) {
                    int n = g_format(page, capacity, "<html><body style=\"margin:8px\"><pre style=\"white-space:pre-wrap\">");
                    n = append_escaped(page, n, capacity, r.data, r.length);
                    n += g_format(page + n, capacity - n, "</pre></body></html>");
                    html = page; length = n;
                }
            } else {   /* the Content-Type header, else <meta charset>, else a guess for bytes that are not UTF-8 */
                charset = web_charset_of(r.type, g_len(r.type));
                if (!charset) charset = web_charset_of(r.data, r.length);
                if (!charset && !w_valid_utf8(r.data, r.length)) charset = WCS_1251;
                html = web_convert(r.data, r.length, charset, &length);
            }
        }
    }
    if (!html) { html = "<html><body>Out of memory</body></html>"; length = g_len(html); }
    LoadJob job = {html, length, r.url, 0, s->images, charset};
    on_big_stack(parse_job, &job);
    s->doc = job.doc;
    if (image_page) {
        for (WNode *n = s->doc->body ? s->doc->body->first : 0; n; n = n->next)
            if (n->tag == T_IMG) { n->image = web_png((const unsigned char *)r.data, r.length); if (!n->image) { s->doc = 0; } break; }
        if (s->doc) { s->doc->mark = web_mark(); }
    }
    if (!s->doc) { loading_window = 0; s->loading = 0; w->busy = 0; bnet_set_idle(0); bnet_close_idle(); return; }
    {   /* the address bar shows %D0%92... as letters when they spell UTF-8 */
        char shown[1024]; int n = 0;
        for (const char *p = r.url; *p && n < (int)sizeof(shown) - 1; p++) {
            int hi = p[0] == '%' && p[1] && p[2] ? w_lower(p[1]) : 0, lo = hi ? w_lower(p[2]) : 0;
            int h = hi >= '0' && hi <= '9' ? hi - '0' : hi >= 'a' && hi <= 'f' ? hi - 'a' + 10 : -1;
            int l = lo >= '0' && lo <= '9' ? lo - '0' : lo >= 'a' && lo <= 'f' ? lo - 'a' + 10 : -1;
            if (h >= 8 && l >= 0) { shown[n++] = (char)(h * 16 + l); p += 2; }   /* only bytes >= 0x80: %20, %2F stay */
            else shown[n++] = *p;
        }
        shown[n] = 0;
        g_edit_set(&s->address, g_equal(r.url, HOME) ? "" : w_valid_utf8(shown, n) ? shown : r.url);
    }
    relayout(w);
    const char *hash = 0;
    for (const char *p = r.url; *p; p++) if (*p == '#') hash = p + 1;
    if (hash) { int y = web_anchor_y(s->doc, hash); if (y >= 0) { s->scroll = y; clamp_scroll(w); } }
    if (s->failed) g_format(s->status, sizeof(s->status), "%s: %s", g_tr("Cannot open the page"), r.error);
    else if (r.error[0] && !web_out_of_memory) g_format(s->status, sizeof(s->status), "%s (%s)", g_tr("Partly loaded"), r.error);
    else if (!web_out_of_memory) g_format(s->status, sizeof(s->status), g_tr("Done: %d KB, %d elements"), (int)(web_used() / 1024), s->doc->item_count);
    title(w);
    {   /* a page made of scripts: say so instead of a white page */
        int words = 0, scripts = 0;
        for (int i = 0; i < s->doc->item_count && words < 12; i++) if (s->doc->items[i].kind == WI_TEXT) words++;
        for (WNode *n = s->doc->root; n;) {
            if (n->type == WN_ELEMENT && n->tag == T_SCRIPT) scripts++;
            if (n->first) { n = n->first; continue; }
            while (n && !n->next) n = n->parent;
            if (n) n = n->next;
        }
        s->needs_script = 0;
        (void)words; (void)scripts;
        if (web_js_error(s->doc)[0]) g_format(s->status, sizeof(s->status), "JavaScript: %s", web_js_error(s->doc));
        if (s->needs_script) g_copy(s->status, g_tr("This page is built by JavaScript, which Gouse Web does not run"), sizeof(s->status));
    }
    loading_window = 0;
    s->loading = 0; w->busy = 0;
    bnet_set_idle(0);
    bnet_close_idle();
    jar_write();
}
static void autofill(GWindow *w);
static void remember(GWindow *w, const char *url)
{
    State *s = S;
    if (s->history_count && s->history_at >= 0 && g_equal(s->history[s->history_at], url)) return;
    if (s->history_at >= 0) s->history_scroll[s->history_at] = s->scroll;
    s->history_count = s->history_at + 1;
    if (s->history_count == HISTORY) {
        for (int i = 1; i < HISTORY; i++) { g_copy(s->history[i - 1], s->history[i], 512); s->history_scroll[i - 1] = s->history_scroll[i]; }
        s->history_count--;
    }
    g_copy(s->history[s->history_count], url, 512);
    s->history_scroll[s->history_count] = 0;
    s->history_at = s->history_count++;
}
/* Navigation happens in the next tick, so the click is drawn first. */
static void go(GWindow *w, const char *url, const char *post, int history)
{
    State *s = S;
    g_copy(s->pending, url, sizeof(s->pending));
    s->pending_post = post != 0;
    if (post) g_copy(s->pending_body, post, sizeof(s->pending_body));
    s->pending_history = history;
    s->pending_delay = 2;
    s->address_focus = 0;
    g_format(s->status, sizeof(s->status), "%s %s", g_tr("Loading"), url);
    g_invalidate(w);
}
static void open_url(GWindow *w, const char *href)
{
    State *s = S;
    if (!href) return;
    if (w_starts_ci(href, "javascript:")) { g_copy(s->status, g_tr("javascript: links are not supported; use event handlers"), sizeof(s->status)); return; }
    if (w_starts_ci(href, "mailto:") || w_starts_ci(href, "tel:")) { g_copy(s->status, href, sizeof(s->status)); return; }
    char url[1024];
    w_resolve_url(s->doc ? s->doc->base : "about:home", href, url, sizeof(url));
    /* a fragment of this page: just scroll */
    const char *hash = 0;
    for (const char *p = url; *p; p++) if (*p == '#') hash = p;
    if (hash && s->doc && s->history_at >= 0) {
        int length = (int)(hash - url);
        const char *now = s->history[s->history_at];
        int same = g_len(now) >= length;
        for (int i = 0; i < length && same; i++) if (now[i] != url[i]) same = 0;
        if (same && (now[length] == 0 || now[length] == '#')) {
            int y = web_anchor_y(s->doc, hash + 1);
            if (y >= 0 || !hash[1]) { s->scroll = y > 0 ? y : 0; clamp_scroll(w); g_invalidate(w); return; }
        }
    }
    go(w, url, 0, 1);
}
/* What the address bar means: an address, a path or words to search for. */
static void address_entered(GWindow *w)
{
    const char *t = S->address.text;
    while (*t == ' ') t++;
    char url[1024];
    int has_space = 0, has_dot = 0, has_scheme = 0;
    for (const char *p = t; *p; p++) { if (*p == ' ') has_space = 1; if (*p == '.') has_dot = 1; if (*p == ':' && p[1] == '/') has_scheme = 1; }
    if (!*t) g_copy(url, HOME, sizeof(url));
    else if (has_scheme || w_starts_ci(t, "about:") || t[0] == '/') g_copy(url, t, sizeof(url));
    else if (has_dot && !has_space) g_format(url, sizeof(url), "https://%s", t);
    else {
        char query[512];
        w_url_encode(t, query, sizeof(query));
        g_format(url, sizeof(url), "https://lite.duckduckgo.com/lite/?q=%s", query);
    }
    go(w, url, 0, 1);
}
void browser_open_url(GWindow *w, const char *url) { if (w && w->app && S) go(w, url, 0, 1); }

static void open(GWindow *w, const char *argument)
{
    State *s = S;
    s->zoom = 100; s->images = 1; s->history_at = -1;
    go(w, argument && argument[0] ? argument : HOME, 0, 1);
    s->pending_delay = 1;
    g_set_title(w, "Gouse Web");
}
/* changes: 0 nothing, 1 repaint (canvas, title), 2 the DOM changed: lay out again. */
typedef struct { WDoc *doc; unsigned int now; int width,height,changes; WNode *target; const char *type; int x,y,key,handled; } ScriptJob;
static void script_job(void *arg) {
    ScriptJob *j=arg;
    if(j->type) { j->handled=web_js_event(j->doc,j->target,j->type,j->x,j->y,j->key); j->changes=web_js_changes(j->doc); }
    else j->changes=web_js_tick(j->doc,j->now,j->width,j->height);
}
static int script_event(GWindow *w,WNode *target,const char *type,int x,int y,int key) {
    if(!S->doc || !S->doc->js) return 0;
    ScriptJob j={0};j.doc=S->doc;j.target=target;j.type=type;j.x=x;j.y=y;j.key=key;
    on_big_stack(script_job,&j);
    if(j.changes>=2) relayout(w);
    if(j.changes) { title(w); g_invalidate(w); }
    return j.handled;
}
static void tick(GWindow *w, int dt)
{
    State *s = S;
    (void)dt;
    if (s->loading) return;
    if (s->pending[0] && --s->pending_delay <= 0) {
        char url[1024], body[2048];
        g_copy(url, s->pending, sizeof(url));
        g_copy(body, s->pending_body, sizeof(body));
        int post = s->pending_post, history = s->pending_history;
        s->pending[0] = 0;
        if (history) remember(w, url);
        load(w, url, post ? body : 0);
        autofill(w);
        if (s->login_pending && s->doc && !s->failed) {
            char text[300], host[128];
            const char *h = s->login_url;
            while (*h && *h != ':') h++;
            if (*h) h += 3;
            int k = 0; while (h[k] && h[k] != '/' && h[k] != ':' && k < 127) { host[k] = h[k]; k++; }
            host[k] = 0;
            g_format(text, sizeof(text), "Save the password for %s%s%s?", host, s->login_user[0] ? " / " : "", s->login_user);
            g_confirm(w, C_SAVE_PASSWORD, "Gouse Web", text);
        }
        if (s->doc && s->history_at >= 0 && s->history_at < s->history_count) {
            /* redirects: the history keeps where we ended */
            const char *final = s->address.text[0] ? s->address.text : HOME;
            if (history) g_copy(s->history[s->history_at], final, 512);
            if (!history && s->history_scroll[s->history_at]) { s->scroll = s->history_scroll[s->history_at]; clamp_scroll(w); }
        }
        g_invalidate(w);
    }
    if (s->doc && s->doc->js) {
        int px,py,pw,ph; page_rect(w,&px,&py,&pw,&ph);
        ScriptJob j={0};j.doc=s->doc;j.now=g_now();j.width=pw;j.height=ph;
        on_big_stack(script_job,&j);
        if(j.changes>=2) relayout(w);
        if(j.changes) { title(w); g_invalidate(w); }
        if(web_js_error(s->doc)[0]) g_format(s->status,sizeof(s->status),"JavaScript: %s",web_js_error(s->doc));
    }
    if (s->relayout_at && (int)(g_now() - (unsigned)s->relayout_at) >= 0) { s->relayout_at = 0; relayout(w); g_invalidate(w); }
}
static const char *const labels_en[] = {"<", ">", "Reload", "Home"};
static void paint(GWindow *w, BgxCanvas *c)
{
    State *s = S;
    bgx_fill(c, 0, 0, w->w, TOOLBAR_H, g_theme.face);
    static const char *const labels_ru[] = {"<", ">", "Обновить", "Домой"};
    static const char *const stop_en[] = {"<", ">", "Stop", "Home"}, *const stop_ru[] = {"<", ">", "Стоп", "Домой"};
    const char *const *labels = s->loading ? (g_russian ? stop_ru : stop_en) : g_russian ? labels_ru : labels_en;
    int x = 4;
    for (int i = 0; i < 4; i++) {
        int bw = i < 2 ? 28 : bgx_text_width(labels[i], 1) + 14;
        int enabled = i == 0 ? s->history_at > 0 : i == 1 ? s->history_at + 1 < s->history_count : 1;
        g_button(w, c, ID_BACK + i, x, 4, bw, 24, labels[i]);
        if (!enabled) bgx_blend(c, x + 1, 5, bw - 2, 22, g_theme.face, 150);
        x += bw + 3;
    }
    int go_w = bgx_text_width(g_russian ? "Перейти" : "Go", 1) + 16;
    int edit_w = w->w - x - go_w - 10;
    g_edit_draw(c, x + 2, 5, edit_w, &s->address, s->address_focus && g_focused(w));
    if (s->address_focus && s->address_fresh && s->address.text[0]) {   /* all selected */
        int tw = (bgx_text_width)(s->address.text, 1);
        if (tw > edit_w - 8) tw = edit_w - 8;
        bgx_blend(c, x + 5, 8, tw + 2, 16, g_theme.highlight, 110);
    }
    if (!s->address.text[0] && !s->address_focus) g_text(c, x + 8, 9, "Search or type an address", g_theme.shadow);
    g_zone(w, ID_ADDRESS, x + 2, 5, edit_w, 22);
    g_button(w, c, ID_GO, w->w - go_w - 4, 4, go_w, 24, g_russian ? "Перейти" : "Go");
    int px, py, pw, ph;
    page_rect(w, &px, &py, &pw, &ph);
    if (s->doc) {
        WCanvas canvas = {c->pixels, c->width, c->height};
        web_paint(s->doc, &canvas, px, py, pw, ph, s->scroll);
    } else {
        bgx_fill(c, px, py, pw, ph, 0xffffff);
        if (s->loading) {
            const char *text = g_tr("Loading the page... (Esc or Stop to cancel)");
            g_text(c, px + (pw - bgx_text_width(text, 1)) / 2, py + ph / 2 - 8, text, 0x606a78);
        }
    }
    if (s->doc && s->needs_script) {
        const char *text = g_tr("This page needs JavaScript, which Gouse Web does not run.");
        bgx_fill(c, px, py, pw, 24, 0xfff3c4);
        bgx_fill(c, px, py + 24, pw, 1, 0xd8c070);
        g_text_fit(c, px + 8, py + 4, pw - 16, text, 0x5a4400);
    }
    g_scrollbar(c, pw, py, ph, s->doc ? s->doc->height : 1, s->scroll, ph);
    bgx_fill(c, 0, w->h - STATUS_H, w->w, STATUS_H, g_theme.face);
    bgx_fill(c, 0, w->h - STATUS_H, w->w, 1, g_theme.shadow);
    g_text_fit_raw(c, 6, w->h - STATUS_H + 2, w->w - 70, s->hover[0] ? s->hover : s->status, g_theme.text);
    if (s->zoom != 100) { char z[16]; g_format(z, sizeof(z), "%d%%", s->zoom); g_text_raw(c, w->w - 52, w->h - STATUS_H + 2, z, g_theme.text); }
}
static void scroll_by(GWindow *w, int dy) { S->scroll += dy; clamp_scroll(w); g_invalidate(w); }
static WControl *focused_control(GWindow *w)
{
    WDoc *d = S->doc;
    return d && d->focused >= 0 && d->focused < d->control_count ? d->controls[d->focused] : 0;
}
/* The login in a form: its password field and the text field before it. */
static void remember_login(GWindow *w, WNode *form)
{
    WDoc *d = S->doc;
    WControl *password = 0, *user = 0;
    for (int i = 0; i < d->control_count; i++) {
        WControl *k = d->controls[i];
        if (k->form != form) continue;
        if (k->type == WC_PASSWORD && k->value[0] && !password) password = k;
        else if (k->type == WC_TEXT && !password && w_attr(k->node, "name")) user = k;
    }
    S->login_pending = 0;
    if (!password || !w_attr(password->node, "name")) return;
    const char *url = d->url;
    if (jar_password_known(url, user ? user->value : "", password->value)) return;
    g_copy(S->login_url, url, sizeof(S->login_url));
    g_copy(S->login_user_field, user ? w_attr(user->node, "name") : "", sizeof(S->login_user_field));
    g_copy(S->login_user, user ? user->value : "", sizeof(S->login_user));
    g_copy(S->login_password_field, w_attr(password->node, "name"), sizeof(S->login_password_field));
    g_copy(S->login_password, password->value, sizeof(S->login_password));
    S->login_pending = 1;
}
/* Saved logins fill the empty fields of a page. */
static void autofill(GWindow *w)
{
    WDoc *d = S->doc;
    char user_field[64], user[128], password_field[64], password[128];
    if (!d || jar_password(d->url, user_field, user, password_field, password, 64)) return;
    for (int i = 0; i < d->control_count; i++) {
        WControl *k = d->controls[i];
        if (k->type != WC_PASSWORD || k->value[0]) continue;
        g_copy(k->value, password, k->capacity); k->cursor = g_len(k->value);
        for (int j = i - 1; j >= 0; j--) {
            WControl *u = d->controls[j];
            if (u->form != k->form || u->type != WC_TEXT) continue;
            const char *name = w_attr(u->node, "name");
            if (!u->value[0] && (!user_field[0] || (name && g_equal(name, user_field)) || j == i - 1)) { g_copy(u->value, user, u->capacity); u->cursor = g_len(u->value); }
            break;
        }
        g_copy(S->status, g_tr("The saved password was filled in"), sizeof(S->status));
    }
}
static void submit(GWindow *w, WControl *k)
{
    char url[2048], body[2048];
    remember_login(w, k->form);
    int method = web_submit(S->doc, k, url, sizeof(url), body, sizeof(body));
    if (method < 0) return;
    go(w, url, method ? body : 0, 1);
}
static WControl *first_submit(WDoc *d, WNode *form)
{
    for (int i = 0; i < d->control_count; i++) if (d->controls[i]->form == form && (d->controls[i]->type == WC_SUBMIT || d->controls[i]->type == WC_IMAGE)) return d->controls[i];
    return 0;
}
static void click_control(GWindow *w, WControl *k)
{
    WDoc *d = S->doc;
    int index = -1;
    for (int i = 0; i < d->control_count; i++) if (d->controls[i] == k) index = i;
    switch (k->type) {
    case WC_CHECKBOX: k->checked = !k->checked; break;
    case WC_RADIO: {
        const char *name = w_attr(k->node, "name");
        for (int i = 0; i < d->control_count; i++) {
            WControl *o = d->controls[i];
            if (o->type == WC_RADIO && o->form == k->form && name && w_attr(o->node, "name") && w_eq(w_attr(o->node, "name"), name)) o->checked = 0;
        }
        k->checked = 1;
        break;
    }
    case WC_SELECT: if (k->option_count) k->selected = (k->selected + 1) % k->option_count; break;
    case WC_SUBMIT: case WC_IMAGE: submit(w, k); return;
    case WC_RESET: case WC_BUTTON: case WC_FILE: case WC_HIDDEN: break;
    default: d->focused = index; S->address_focus = 0; k->cursor = g_len(k->value); break;
    }
    g_invalidate(w);
}
static int edit_control(GWindow *w, WControl *k, GEvent *e)
{
    char *v = k->value;
    int length = g_len(v);
    if (e->code == BGX_KEY_LEFT) { k->cursor = g_utf8_prev(v, k->cursor); return 1; }
    if (e->code == BGX_KEY_RIGHT) { k->cursor = g_utf8_next(v, length, k->cursor); return 1; }
    if (e->code == 0xc7) { k->cursor = 0; return 1; }
    if (e->code == 0xcf) { k->cursor = length; return 1; }
    if (e->code == BGX_KEY_ESC || e->code == BGX_KEY_TAB) { S->doc->focused = -1; return 1; }
    if (e->key == '\n') {
        if (k->type == WC_TEXTAREA) { if (length + 1 < k->capacity) { for (int i = length; i >= k->cursor; i--) v[i + 1] = v[i]; v[k->cursor++] = '\n'; } return 1; }
        WControl *button = first_submit(S->doc, k->form);
        if (k->form) {
            if (button) submit(w, button);
            else {   /* a form without a submit button */
                char url[2048], body[2048];
                remember_login(w, k->form);
                int method = web_submit(S->doc, k, url, sizeof(url), body, sizeof(body));
                if (method >= 0) go(w, url, method ? body : 0, 1);
            }
        }
        return 1;
    }
    if (e->key == '\b') {
        if (!k->cursor) return 1;
        int prev = g_utf8_prev(v, k->cursor), gap = k->cursor - prev;
        for (int i = prev; i <= length - gap; i++) v[i] = v[i + gap];
        k->cursor = prev;
        return 1;
    }
    if (e->code == 0xd3 && k->cursor < length) {
        int next = g_utf8_next(v, length, k->cursor), gap = next - k->cursor;
        for (int i = k->cursor; i <= length - gap; i++) v[i] = v[i + gap];
        return 1;
    }
    if (e->key == 3 || e->key == 24) {   /* Ctrl+C copies the field, Ctrl+X cuts it */
        if (length && k->type != WC_PASSWORD) g_clipboard_set(v, length);
        if (e->key == 24 && k->type != WC_PASSWORD) { v[0] = 0; k->cursor = 0; }
        return 1;
    }
    if (e->key == 22) {   /* Ctrl+V */
        for (int i = 0; i < g_clipboard_len && length + 1 < k->capacity; i++) {
            if (g_clipboard[i] == '\n' && k->type != WC_TEXTAREA) break;
            for (int j = length; j >= k->cursor; j--) v[j + 1] = v[j];
            v[k->cursor++] = g_clipboard[i]; length++;
        }
        return 1;
    }
    if (e->key >= 32 && e->key != 127 && !(e->key >= 0x100 && e->key <= 0x108)) {
        char bytes[4];
        int n = g_utf8_put(bytes, e->key);
        if (length + n >= k->capacity) return 1;
        for (int i = length; i >= k->cursor; i--) v[i + n] = v[i];
        for (int i = 0; i < n; i++) v[k->cursor + i] = bytes[i];
        k->cursor += n;
        return 1;
    }
    return 0;
}
static void zoom(GWindow *w, int value)
{
    if (value < 50) value = 50;
    if (value > 300) value = 300;
    if (value == S->zoom) return;
    int px, py, pw, ph;
    page_rect(w, &px, &py, &pw, &ph);
    int top = S->doc && S->doc->height ? S->scroll * 1000 / S->doc->height : 0;
    S->zoom = value;
    relayout(w);
    if (S->doc) { S->scroll = S->doc->height * top / 1000; clamp_scroll(w); }
    g_invalidate(w);
}
static void find_next(GWindow *w)
{
    WDoc *d = S->doc;
    if (!d || !S->find[0]) return;
    int item = web_find(d, S->find, d->find_item);
    d->find_item = item;
    if (item < 0) { g_format(S->status, sizeof(S->status), g_tr("Not found: %s"), S->find); }
    else {
        int px, py, pw, ph;
        page_rect(w, &px, &py, &pw, &ph);
        int y = d->items[item].y;
        if (y < S->scroll + 20 || y > S->scroll + ph - 20) { S->scroll = y - ph / 3; clamp_scroll(w); }
        g_copy(S->status, "", sizeof(S->status));
    }
    g_invalidate(w);
}
static void back_forward(GWindow *w, int step)
{
    State *s = S;
    int at = s->history_at + step;
    if (at < 0 || at >= s->history_count) return;
    s->history_scroll[s->history_at] = s->scroll;
    s->history_at = at;
    go(w, s->history[at], 0, 0);
}
extern void g_desktop_link(const char *name, const char *url);
static void event(GWindow *w, GEvent *e)
{
    State *s = S;
    int px, py, pw, ph;
    page_rect(w, &px, &py, &pw, &ph);
    if (s->loading) {   /* while a page loads: Stop, Esc */
        if ((e->type == EV_COMMAND && e->id == ID_RELOAD) || (e->type == EV_KEY && e->code == BGX_KEY_ESC)) { s->stop = 1; g_copy(s->status, g_tr("Stopping..."), sizeof(s->status)); }
        g_invalidate(w);
        return;
    }
    if (e->type == EV_CONFIRM) {
        if (e->id == C_SAVE_PASSWORD) { jar_save_password(s->login_url, s->login_user_field, s->login_user, s->login_password_field, s->login_password); s->login_pending = 0; jar_write(); }
        else if (e->id == C_CLEAR_COOKIES) { jar_clear_cookies(); jar_write(); g_copy(s->status, g_tr("All cookies are deleted"), sizeof(s->status)); }
        else if (e->id == C_CLEAR_PASSWORDS) { jar_clear_passwords(); jar_write(); g_copy(s->status, g_tr("All saved passwords are deleted"), sizeof(s->status)); }
        g_invalidate(w);
        return;
    }
    switch (e->type) {
    case EV_COMMAND:
        if (e->id == ID_BACK) back_forward(w, -1);
        else if (e->id == ID_FORWARD) back_forward(w, 1);
        else if (e->id == ID_RELOAD) { if (s->history_at >= 0) go(w, s->history[s->history_at], 0, 0); }
        else if (e->id == ID_HOME) go(w, HOME, 0, 1);
        else if (e->id == ID_GO) address_entered(w);
        else if (e->id == ID_ADDRESS) { if (!s->address_focus) s->address_fresh = 1; s->address_focus = 1; if (s->doc) s->doc->focused = -1; }
        break;
    case EV_MENU:
        switch (e->id) {
        case M_OPEN: { char initial[256]; g_path_join(initial, sizeof(initial), g_home(), "index.html"); g_prompt(w, P_OPEN, "Open file", "File or folder to open:", initial); break; }
        case M_SAVE: if (s->source) { char initial[256]; g_path_join(initial, sizeof(initial), g_home(), "page.html"); g_prompt(w, P_SAVE, "Save page", "Save the page as:", initial); } break;
        case M_SOURCE:
            if (s->source) {
                char path[256];
                g_path_join(path, sizeof(path), g_home(), ".gouse-web-source.txt");
                int n = s->source_length > FILE_LIMIT ? FILE_LIMIT : s->source_length;
                if (n > 65535) n = 65535;
                if (!g_write_file(path, s->source, n)) g_open(&app_notepad, path);
            } else g_message(w, "Gouse Web", "This page has no source to show.");
            break;
        case M_EXIT: g_close(w); return;
        case M_COPY_ADDRESS: {
            const char *url = s->history_at >= 0 ? s->history[s->history_at] : "";
            g_clipboard_set(url, g_len(url));
            g_copy(s->status, g_tr("The address is in the clipboard"), sizeof(s->status));
            break;
        }
        case M_COPY_TEXT: if (s->doc) {   /* the page's text, in reading order */
            enum { TEXT = 16384 };
            static char *text;
            if (!text) text = g_alloc(TEXT);
            if (!text) break;
            int n = 0, last_y = -100000;
            for (int i = 0; i < s->doc->item_count && n < TEXT - 2; i++) {
                const WItem *it = &s->doc->items[i];
                if (it->kind != WI_TEXT || !it->text) continue;
                if (n && it->y > last_y + 4) text[n++] = '\n'; else if (n && text[n - 1] != ' ') text[n++] = ' ';
                for (int k = 0; k < it->length && n < TEXT - 2; k++) text[n++] = it->text[k];
                last_y = it->y;
            }
            g_clipboard_set(text, n);
            g_copy(s->status, g_tr("The page's text is in the clipboard"), sizeof(s->status));
        }
            break;
        case M_PASTE_GO: {
            char url[512] = ""; int n = 0;
            for (int i = 0; i < g_clipboard_len && n < 511 && g_clipboard[i] != '\n'; i++) url[n++] = g_clipboard[i];
            url[n] = 0;
            if (n) go(w, url, 0, 1);
            break;
        }
        case M_DOCS: go(w, "about:docs", 0, 1); break;
        case M_RELOAD: if (s->history_at >= 0) go(w, s->history[s->history_at], 0, 0); break;
        case M_ZOOM_IN: zoom(w, s->zoom + 10); break;
        case M_ZOOM_OUT: zoom(w, s->zoom - 10); break;
        case M_ZOOM_RESET: zoom(w, 100); break;
        case M_BACK: back_forward(w, -1); break;
        case M_FORWARD: back_forward(w, 1); break;
        case M_HOME: go(w, HOME, 0, 1); break;
        case M_FIND: g_prompt(w, P_FIND, "Find on page", "Text to find:", s->find); break;
        case M_FIND_NEXT: find_next(w); break;
        case M_IMAGES: s->images = !s->images; g_copy(s->status, g_tr(s->images ? "Pictures on (from the next page)" : "Pictures off (from the next page)"), sizeof(s->status)); break;
        case M_DESKTOP:
            if (s->history_at >= 0) {
                const char *url = s->history[s->history_at];
                g_desktop_link(s->doc && s->doc->title[0] ? s->doc->title : url, g_equal(url, "") ? HOME : url);
                g_copy(s->status, g_tr("A shortcut to this page is on the desktop"), sizeof(s->status));
            }
            break;
        case M_COOKIES: {
            char text[200];
            jar_read();
            g_format(text, sizeof(text), "Gouse Web keeps %d cookies (logins and site settings).\nDelete all of them?", jar_cookie_count());
            g_confirm(w, C_CLEAR_COOKIES, "Cookies", text);
            break;
        }
        case M_PASSWORDS: {
            char text[1024];
            jar_read();
            int n = jar_password_count();
            if (!n) { g_message(w, "Saved passwords", "No passwords are saved yet.\nGouse Web offers to save one when you log in to a site."); break; }
            int at = g_format(text, sizeof(text), "%s\n", g_tr("Saved passwords:"));
            for (int i = 0; i < n && i < 8; i++) {
                const char *user, *site = jar_password_site(i, &user);
                at += g_format(text + at, sizeof(text) - at, "%s  %s\n", site, user);
            }
            g_append(text, g_tr("\nForget all of them?"), sizeof(text));
            g_confirm(w, C_CLEAR_PASSWORDS, "Saved passwords", text);
            break;
        }
        case M_ABOUT: g_message(w, "Gouse Web", "Gouse Web 1.0 for Happy Gouse\nHTML5 and CSS3: selectors, cascade,\nvariables, flexbox, grid, tables, floats,\npositioning, forms and PNG pictures.\nQuickJS JavaScript and Canvas2D.\nWebGL is not available yet."); break;
        }
        break;
    case EV_PROMPT:
        if (!e->text[0]) break;
        if (e->id == P_OPEN) go(w, e->text, 0, 1);
        else if (e->id == P_SAVE && s->source) { if (g_write_file(e->text, s->source, s->source_length > FILE_LIMIT ? FILE_LIMIT : s->source_length)) g_error(w, "Gouse Web", "Cannot save the file there."); }
        else if (e->id == P_DOWNLOAD && s->source) {
            if (s->source_length > FILE_LIMIT) g_error(w, "Gouse Web", "Barnix files are limited to 16 MiB; the download is larger.");
            else if (g_write_file(e->text, s->source, s->source_length)) g_error(w, "Gouse Web", "Cannot save the file there.");
            else g_format(s->status, sizeof(s->status), "%s %s", g_tr("Saved"), e->text);
        } else if (e->id == P_FIND) { g_copy(s->find, e->text, sizeof(s->find)); if (s->doc) s->doc->find_item = -1; find_next(w); }
        break;
    case EV_KEY: {
        WControl *k = focused_control(w);
        if (s->address_focus) {
            if (s->address_fresh) {
                s->address_fresh = 0;
                int printable = e->key >= 32 && e->key != 127 && !(e->key >= 0x100 && e->key <= 0x108);
                if (printable || e->key == '\b' || e->code == 0xd3) { g_edit_set(&s->address, ""); if (!printable) break; }
            }
            int r = g_edit_key(&s->address, e);
            if (r == 2) address_entered(w);
            else if (r == 3) s->address_focus = 0;
            break;
        }
        int jskey=e->key;
        if(e->code==BGX_KEY_LEFT)jskey=37;else if(e->code==BGX_KEY_UP)jskey=38;else if(e->code==BGX_KEY_RIGHT)jskey=39;else if(e->code==BGX_KEY_DOWN)jskey=40;
        if(!g_ctrl() && script_event(w,k?k->node:0,"keydown",0,0,jskey)) break;
        if (k && edit_control(w, k, e)) break;
        int line = 40;
        if (e->key == 12) { s->address_focus = 1; s->address_fresh = 1; s->address.cursor = g_len(s->address.text); }   /* Ctrl+L */
        else if (e->key == 6) g_prompt(w, P_FIND, "Find on page", "Text to find:", s->find);    /* Ctrl+F */
        else if (e->code == 0x3d) find_next(w);                                                    /* F3 */
        else if (e->code == 0x3f) { if (s->history_at >= 0) go(w, s->history[s->history_at], 0, 0); }   /* F5 */
        else if (g_ctrl() && (e->code == 0x0d || e->code == 0x4e)) zoom(w, s->zoom + 10);
        else if (g_ctrl() && (e->code == 0x0c || e->code == 0x4a)) zoom(w, s->zoom - 10);
        else if (g_ctrl() && e->code == 0x0b) zoom(w, 100);
        else if (e->code == BGX_KEY_UP) scroll_by(w, -line);
        else if (e->code == BGX_KEY_DOWN) scroll_by(w, line);
        else if (e->code == 0xc9) scroll_by(w, -(ph - line));
        else if (e->code == 0xd1 || e->key == ' ') scroll_by(w, g_shift() && e->key == ' ' ? -(ph - line) : ph - line);
        else if (e->code == 0xc7) scroll_by(w, -(1 << 28));
        else if (e->code == 0xcf) scroll_by(w, 1 << 28);
        else if (e->key == '\b' || (e->code == BGX_KEY_LEFT && (bgx_key_held(0x38) || bgx_key_held(0xb8)))) back_forward(w, -1);
        else if (e->code == BGX_KEY_RIGHT && (bgx_key_held(0x38) || bgx_key_held(0xb8))) back_forward(w, 1);
        break;
    }
    case EV_KEY_UP:
        if(!s->address_focus) {
            int key=e->key;
            if(e->code==BGX_KEY_LEFT)key=37;else if(e->code==BGX_KEY_UP)key=38;else if(e->code==BGX_KEY_RIGHT)key=39;else if(e->code==BGX_KEY_DOWN)key=40;
            script_event(w,0,"keyup",0,0,key);
        }
        break;
    case EV_WHEEL: scroll_by(w, e->id * 48); break;
    case EV_MOUSE_DOWN:
        if (e->y < TOOLBAR_H) break;
        if (e->x >= pw && e->y >= py && e->y < py + ph) {   /* the scrollbar */
            if (s->doc && s->doc->height > ph) { s->scroll = (e->y - py) * (s->doc->height - ph) / (ph > 1 ? ph : 1); clamp_scroll(w); s->dragging_bar = 1; }
            break;
        }
        s->address_focus = 0;
        if (s->doc) s->doc->focused = -1;
        if (s->doc && e->y >= py && e->y < py + ph && e->button == 1) {
            WItem *it = web_hit(s->doc, e->x - px, e->y - py, s->scroll);
            WNode *event_node=it?it->node:0;
            if(script_event(w,event_node,"mousedown",e->x-px,e->y-py,0)) break;
            if(script_event(w,event_node,"click",e->x-px,e->y-py,0)) break;
            it=web_hit(s->doc,e->x-px,e->y-py,s->scroll);
            WNode *control_node = 0;
            if (it) for (WNode *n = it->node; n; n = n->parent) if (n->control) { control_node = n; break; }
            if (control_node) click_control(w, control_node->control);
            else if (it && it->link) open_url(w, w_attr(it->link, "href"));
        }
        break;
    case EV_MOUSE_UP: s->dragging_bar = 0; if(e->y>=py&&e->y<py+ph)script_event(w,0,"mouseup",e->x-px,e->y-py,0); break;
    case EV_MOUSE_MOVE: {
        if (s->dragging_bar && s->doc && s->doc->height > ph) { s->scroll = (e->y - py) * (s->doc->height - ph) / (ph > 1 ? ph : 1); clamp_scroll(w); break; }
        char hover[300] = "";
        if (s->doc && e->y >= py && e->y < py + ph && e->x < pw) {
            WItem *it = web_hit(s->doc, e->x - px, e->y - py, s->scroll);
            if (it && it->link && w_attr(it->link, "href")) w_resolve_url(s->doc->base, w_attr(it->link, "href"), hover, sizeof(hover));
        }
        if(s->doc && e->y>=py && e->y<py+ph && e->x<pw) { WItem *it=web_hit(s->doc,e->x-px,e->y-py,s->scroll); script_event(w,it?it->node:0,"mousemove",e->x-px,e->y-py,0); }
        if (!g_equal(hover, s->hover)) { g_copy(s->hover, hover, sizeof(s->hover)); g_invalidate(w); }
        return;
    }
    case EV_RESIZE: s->relayout_at = (int)(g_now() + 200); if (!s->relayout_at) s->relayout_at = 1; break;
    }
    g_invalidate(w);
}
static void close_browser(GWindow *w) { web_js_destroy(S->doc); S->doc=0; }
static const GMenu menus[] = {
    {"File", {{"Open file...", M_OPEN}, {"Save page as...", M_SAVE}, {"-", 0}, {"Exit", M_EXIT}}},
    {"Edit", {{"Copy address", M_COPY_ADDRESS}, {"Copy page text", M_COPY_TEXT}, {"Paste and go", M_PASTE_GO}, {"-", 0},
              {"Find...   Ctrl+F", M_FIND}}},
    {"View", {{"Reload        F5", M_RELOAD}, {"Find...   Ctrl+F", M_FIND}, {"Find next     F3", M_FIND_NEXT}, {"-", 0},
              {"Zoom in   Ctrl++", M_ZOOM_IN}, {"Zoom out  Ctrl+-", M_ZOOM_OUT}, {"Normal size", M_ZOOM_RESET}, {"-", 0},
              {"Pictures on/off", M_IMAGES}, {"Page source", M_SOURCE}, {"Add to desktop", M_DESKTOP}}},
    {"Privacy", {{"Cookies...", M_COOKIES}, {"Saved passwords...", M_PASSWORDS}}},
    {"Go", {{"Back   Backspace", M_BACK}, {"Forward", M_FORWARD}, {"Home page", M_HOME}, {"Manuals", M_DOCS}, {"-", 0}, {"About Gouse Web", M_ABOUT}}},
};
const GApp app_browser = {"Gouse Web", "Browse the web: HTML5 and CSS3", ICON_BROWSER, 760, 520, 360, 240,
                          APP_RESIZABLE | APP_SINGLE | APP_FULLSCREEN, menus, 5, sizeof(State), open, event, tick, paint, close_browser};
