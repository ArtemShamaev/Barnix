/* Renders a page with the Gouse Web engine on the host (tests/web_test.py).
 *   web_render FONT.bwf PAGE.html WIDTH OUT.ppm [VIEWPORT_H]
 * Local files only: links are resolved against the page's directory. Prints
 * the title, the document height and the display item count. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <unistd.h>
#include <time.h>
#include "apps/gouse/web.h"

static char directory[512];
int web_fetch(const char *url, WebResource *out, int max, const char *post, const char *type)
{
    (void)post; (void)type;
    memset(out, 0, sizeof(*out));
    char path[1024];
    const char *p = url;
    if (!strncmp(p, "file://", 7)) p += 7;
    if (!strncmp(url, "http", 4)) {
        if (!getenv("WEB_ONLINE")) { strcpy(out->error, "offline"); return -1; }
        static int counter;
        char command[1400];
        snprintf(path, sizeof(path), "/tmp/claude-1000/web_render_%d_%d", getpid(), counter++);
        snprintf(command, sizeof(command), "curl -sL --max-time 20 -A 'GouseWeb/1.0' -o '%s' '%s'", path, url);
        if (system(command)) { strcpy(out->error, "curl failed"); return -1; }
    } else snprintf(path, sizeof(path), "%s", p);
    FILE *f = fopen(path, "rb");
    if (!f) { strcpy(out->error, "missing"); return -1; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n > max) n = max;
    out->data = web_alloc_big((unsigned)n + 1);
    if (!out->data) { fclose(f); return -1; }
    out->length = (int)fread(out->data, 1, (size_t)n, f);
    out->data[out->length] = 0;
    fclose(f);
    out->status = 200;
    snprintf(out->url, sizeof(out->url), "%s", url);
    return 0;
}
long web_inflate(const void *in, unsigned long in_length, void *out, unsigned long capacity, int zlib)
{
    z_stream z;
    memset(&z, 0, sizeof(z));
    if (inflateInit2(&z, zlib ? 15 : -15) != Z_OK) return -1;
    z.next_in = (unsigned char *)in; z.avail_in = (unsigned)in_length;
    z.next_out = out; z.avail_out = (unsigned)capacity;
    int r = inflate(&z, Z_FINISH);
    long n = (long)z.total_out;
    inflateEnd(&z);
    return r == Z_STREAM_END || r == Z_OK || r == Z_BUF_ERROR ? n : -1;
}
void web_progress(const char *text) { fprintf(stderr, "[progress] %s\n", text); }
int web_idle(void) { return 0; }
unsigned int web_time(void) { return 0; }

int main(int argc, char **argv)
{
    if (argc == 4 && !strcmp(argv[1], "--url")) {   /* w_resolve_url BASE HREF */
        char out[1024];
        w_resolve_url(argv[2], argv[3], out, sizeof(out));
        puts(out);
        return 0;
    }
    if (argc < 5) { fprintf(stderr, "usage: web_render FONT PAGE WIDTH OUT.ppm [VIEWPORT_H]\n"); return 2; }
    static unsigned char font_memory[4 << 20];
    FILE *ff = fopen(argv[1], "rb");
    if (!ff) { perror("font"); return 2; }
    static unsigned char font_file[1 << 20];
    int fl = (int)fread(font_file, 1, sizeof(font_file), ff);
    fclose(ff);
    if (web_font_init(font_file, (unsigned)fl, font_memory, sizeof(font_memory))) { fprintf(stderr, "font failed\n"); return 1; }
    unsigned int arena = 256u << 20;
    web_arena(malloc(arena), arena);
    snprintf(directory, sizeof(directory), "%s", argv[2]);
    char *slash = strrchr(directory, '/');
    if (slash) slash[1] = 0; else strcpy(directory, "./");
    char url[1024];
    snprintf(url, sizeof(url), "file://%s", argv[2]);
    WebResource page;
    if (web_fetch(url, &page, 8 << 20, 0, 0)) { fprintf(stderr, "cannot read page\n"); return 1; }
    int length;
    char *html = web_to_utf8(page.data, page.length, &length);
    WDoc *doc = web_parse(html, length, url);
    web_load_resources(doc, 1);
    int width = atoi(argv[3]), view = argc > 5 ? atoi(argv[5]) : 600;
    clock_t t0 = clock();
    web_layout(doc, width, view);
    fprintf(stderr, "layout: %.1f ms\n", (clock() - t0) * 1000.0 / CLOCKS_PER_SEC);
    int height = doc->height < 40000 ? doc->height : 40000;
    if (height < 1) height = 1;
    WCanvas canvas = {malloc((size_t)width * height * 4), width, height};
    web_paint(doc, &canvas, 0, 0, width, height, 0);
    FILE *out = fopen(argv[4], "wb");
    fprintf(out, "P6\n%d %d\n255\n", width, height);
    for (int i = 0; i < width * height; i++) { unsigned int p = canvas.pixels[i]; fputc(p >> 16 & 255, out); fputc(p >> 8 & 255, out); fputc(p & 255, out); }
    fclose(out);
    printf("title: %s\nheight: %d\nitems: %d\nrules: %d\nmemory: %u\n", doc->title, doc->height, doc->item_count, doc->rule_count, web_used());
    for (int i = 0; i < doc->item_count && argc > 6; i++) {
        WItem *it = &doc->items[i];
        printf("%d kind=%d x=%d y=%d w=%d h=%d color=%08x fixed=%d %.*s%s\n", i, it->kind, it->x, it->y, it->w, it->h, it->color, it->fixed,
               it->kind == 3 || it->kind == 9 ? it->length : 0, it->text ? it->text : "", it->link ? " [link]" : "");
    }
    for (int i = 0; i < doc->control_count && argc > 6; i++)
        if (doc->controls[i]->type == WC_SUBMIT) {
            char url[2048], body[2048];
            int method = web_submit(doc, doc->controls[i], url, sizeof(url), body, sizeof(body));
            printf("submit %d %s %s\n", method, url, method == 1 ? body : "");
            break;
        }
    return 0;
}
