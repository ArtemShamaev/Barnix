/* Host test of Barnix OpenGL (BDK/runtime/gl.c, gl_raster.c, glu.c).
 * Renders off-screen with bxgl_context, checks pixels and writes PPM files
 * to the directory given as the first argument. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <barnix_opengl.h>

const BarnixAPI *barnix;
static int job_order;
/* Bands run in reverse or forward order: results must not depend on it. */
void bgx_parallel(void (*job)(void *, unsigned int), void *argument, unsigned int count)
{
    for (unsigned int i = 0; i < count; i++) job(argument, job_order ? count - 1 - i : i);
}
unsigned int bgx_cpus(void) { return 4; }
int bgx_display(int *w, int *h) { *w = 640; *h = 400; return 0; }
int bgx_open(BgxCanvas *c, unsigned int *p, int w, int h) { c->pixels = p; c->width = w; c->height = h; return 0; }
int bgx_present(const BgxCanvas *c) { (void)c; return 0; }
void bgx_close(void) {}
unsigned int bgx_millis(void) { return 0; }

#define W 320
#define H 240
static unsigned int color[W * H];
static float depth[W * H];
static unsigned char heap[8 << 20];
static int failures;
static const char *out_dir = ".";

static void check(int ok, const char *what)
{
    if (!ok) { printf("FAIL: %s\n", what); failures++; }
}
static unsigned int px(int x, int y) { return color[(H - 1 - y) * W + x] & 0xffffff; }   /* GL window coords */
static void save(const char *name)
{
    glFinish();
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.ppm", out_dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) { unsigned char rgb[3] = {color[i] >> 16, color[i] >> 8, color[i]}; fwrite(rgb, 1, 3, f); }
    fclose(f);
}
static void context(void)
{
    if (bxgl_context(color, depth, W, H, heap, sizeof(heap))) { printf("FAIL: context\n"); exit(1); }
}

/* A fan of many thin triangles plus a triangle mesh with additive blending:
 * every covered pixel must be drawn exactly once. */
static void test_watertight(void)
{
    context();
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, W, 0, H, -1, 1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE);
    glColor3ub(100, 0, 0);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(80.3f, 120.7f);
    for (int i = 0; i <= 97; i++) {
        double a = 2 * M_PI * i / 97;
        glVertex2f(80.3f + (float)(cos(a) * 70.1), 120.7f + (float)(sin(a) * 70.1));
    }
    glEnd();
    srand(7);
    float grid[9][9][2];
    for (int j = 0; j < 9; j++)
        for (int i = 0; i < 9; i++) {
            float jx = (i == 0 || i == 8) ? 0 : (rand() % 1000) / 100.0f - 5, jy = (j == 0 || j == 8) ? 0 : (rand() % 1000) / 100.0f - 5;
            grid[j][i][0] = 170.0f + i * 17.3f + jx; grid[j][i][1] = 40.0f + j * 19.1f + jy;
        }
    glBegin(GL_TRIANGLES);
    for (int j = 0; j < 8; j++)
        for (int i = 0; i < 8; i++) {
            glVertex2fv(grid[j][i]); glVertex2fv(grid[j][i + 1]); glVertex2fv(grid[j + 1][i + 1]);
            glVertex2fv(grid[j][i]); glVertex2fv(grid[j + 1][i + 1]); glVertex2fv(grid[j + 1][i]);
        }
    glEnd();
    glFinish();
    int doubled = 0, holes = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            unsigned int r = px(x, y) >> 16;
            if (r > 100) doubled++;
            double dx = x + 0.5 - 80.3, dy = y + 0.5 - 120.7;
            if (dx * dx + dy * dy < 68 * 68 && r != 100) holes++;
            if (x > 175 && x < 305 && y > 47 && y < 186 && r != 100) holes++;
        }
    printf("watertight: %d doubled, %d holes\n", doubled, holes);
    check(!doubled, "no pixel drawn twice");
    check(!holes, "no cracks inside meshes");
    save("gl_watertight");
}

static void test_depth_and_lists(void)
{
    context();
    glClearColor(0.1f, 0.1f, 0.2f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(-1, 1, -1, 1, -1, 1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    GLuint list = glGenLists(1);
    glNewList(list, GL_COMPILE);
    glBegin(GL_QUADS);
    glVertex3f(-0.5f, -0.5f, 0); glVertex3f(0.5f, -0.5f, 0); glVertex3f(0.5f, 0.5f, 0); glVertex3f(-0.5f, 0.5f, 0);
    glEnd();
    glEndList();
    check(px(W / 2, H / 2) == 0x1a1a33, "GL_COMPILE does not draw");
    /* Near (green, z=-0.5 -> nearer) drawn first, far (red) must not overwrite it. */
    glPushMatrix(); glTranslatef(0, 0, 0.5f); glColor3f(0, 1, 0); glCallList(list); glPopMatrix();
    glPushMatrix(); glTranslatef(0.3f, 0.3f, -0.5f); glColor3f(1, 0, 0); glCallList(list); glPopMatrix();
    glFinish();
    check(px(W / 2, H / 2) == 0x00ff00, "depth test keeps the nearer quad");
    check(px(W * 3 / 4 + 10, H * 3 / 4 + 10) == 0xff0000, "far quad visible where uncovered");
    unsigned char rgb[3];
    glReadPixels(W / 2, H / 2, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, rgb);
    check(rgb[0] == 0 && rgb[1] == 255 && rgb[2] == 0, "glReadPixels");
    /* Vertex arrays with indices. */
    static const float verts[] = {-0.9f, -0.9f, -0.8f, -0.9f, -0.8f, -0.8f};
    static const unsigned char colors[] = {255, 255, 0, 255, 255, 0, 255, 255, 0};
    static const unsigned short index[] = {0, 1, 2};
    glVertexPointer(2, GL_FLOAT, 0, verts);
    glColorPointer(3, GL_UNSIGNED_BYTE, 0, colors);
    glEnableClientState(GL_VERTEX_ARRAY); glEnableClientState(GL_COLOR_ARRAY);
    glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, index);
    glDisableClientState(GL_COLOR_ARRAY); glDisableClientState(GL_VERTEX_ARRAY);
    glFinish();
    check(px((int)(W * 0.07), (int)(H * 0.06)) == 0xffff00, "glDrawElements");
    check(glGetError() == GL_NO_ERROR, "no GL errors");
    save("gl_depth");
}

static void checker(unsigned char *tex, int n)
{
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++) {
            int on = ((x / (n / 8)) ^ (y / (n / 8))) & 1;
            unsigned char *p = tex + (y * n + x) * 3;
            p[0] = on ? 240 : 40; p[1] = on ? 200 : 60; p[2] = on ? 60 : 160;
        }
}
static void cube(void)
{
    static const float n[6][3] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    static const float v[6][4][3] = {
        {{-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}}, {{1, -1, -1}, {-1, -1, -1}, {-1, 1, -1}, {1, 1, -1}},
        {{1, -1, 1}, {1, -1, -1}, {1, 1, -1}, {1, 1, 1}}, {{-1, -1, -1}, {-1, -1, 1}, {-1, 1, 1}, {-1, 1, -1}},
        {{-1, 1, 1}, {1, 1, 1}, {1, 1, -1}, {-1, 1, -1}}, {{-1, -1, -1}, {1, -1, -1}, {1, -1, 1}, {-1, -1, 1}}};
    static const float t[4][2] = {{0, 0}, {2, 0}, {2, 2}, {0, 2}};
    glBegin(GL_QUADS);
    for (int f = 0; f < 6; f++) {
        glNormal3fv(n[f]);
        for (int k = 0; k < 4; k++) { glTexCoord2fv(t[k]); glVertex3fv(v[f][k]); }
    }
    glEnd();
}
static void test_scene(void)
{
    context();
    glViewport(0, 0, W, H);
    glClearColor(0.55f, 0.65f, 0.8f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    gluPerspective(60, (double)W / H, 0.5, 50);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    gluLookAt(0, 2.5, 7, 0, 0, 0, 0, 1, 0);
    GLfloat light[] = {3, 5, 4, 1}, white[] = {1, 1, 1, 1};
    glEnable(GL_LIGHTING); glEnable(GL_LIGHT0);
    glLightfv(GL_LIGHT0, GL_POSITION, light);
    glEnable(GL_COLOR_MATERIAL);
    glMaterialfv(GL_FRONT, GL_SPECULAR, white);
    glMaterialf(GL_FRONT, GL_SHININESS, 40);
    glEnable(GL_FOG);
    GLfloat fog[] = {0.55f, 0.65f, 0.8f, 1};
    glFogi(GL_FOG_MODE, GL_LINEAR); glFogf(GL_FOG_START, 6); glFogf(GL_FOG_END, 30); glFogfv(GL_FOG_COLOR, fog);

    /* Floor: a large textured quad going into the distance (mipmaps). */
    static unsigned char tex[64 * 64 * 3];
    checker(tex, 64);
    GLuint names[1];
    glGenTextures(1, names);
    glBindTexture(GL_TEXTURE_2D, names[0]);
    gluBuild2DMipmaps(GL_TEXTURE_2D, GL_RGB, 64, 64, GL_RGB, GL_UNSIGNED_BYTE, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glEnable(GL_TEXTURE_2D);
    glColor3f(1, 1, 1);
    glBegin(GL_QUADS);
    glNormal3f(0, 1, 0);
    glTexCoord2f(0, 0); glVertex3f(-30, -1, 30);
    glTexCoord2f(30, 0); glVertex3f(30, -1, 30);
    glTexCoord2f(30, 30); glVertex3f(30, -1, -30);
    glTexCoord2f(0, 30); glVertex3f(-30, -1, -30);
    glEnd();

    glPushMatrix();
    glTranslatef(-1.6f, 0, 0); glRotatef(30, 0, 1, 0); glRotatef(20, 1, 0, 0);
    glColor3f(1, 0.9f, 0.8f);
    cube();
    glPopMatrix();
    glDisable(GL_TEXTURE_2D);

    GLUquadric *q = gluNewQuadric();
    glPushMatrix();
    glTranslatef(1.7f, 0.1f, 0.3f);
    glColor3f(0.9f, 0.2f, 0.2f);
    gluSphere(q, 1.1, 32, 20);
    glPopMatrix();
    glPushMatrix();
    glTranslatef(0, -1, -3); glRotatef(-90, 1, 0, 0);
    glColor3f(0.2f, 0.8f, 0.3f);
    gluCylinder(q, 0.6, 0.2, 2.5, 24, 1);
    glPopMatrix();
    gluDeleteQuadric(q);

    /* Translucent wireframe overlay with blending, no lighting. */
    glDisable(GL_LIGHTING); glDisable(GL_FOG);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glColor4f(1, 1, 0, 0.8f);
    glPushMatrix(); glTranslatef(0, 2.2f, -1); glScalef(0.6f, 0.6f, 0.6f); glRotatef(45, 1, 1, 0); cube(); glPopMatrix();
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); gluOrtho2D(0, W, 0, H);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glColor4f(0, 0, 0, 0.5f);
    glRectf(8, 8, 120, 30);
    glDisable(GL_BLEND);
    glFinish();
    check(glGetError() == GL_NO_ERROR, "scene: no GL errors");
    unsigned int sky = px(W / 2, H - 3);
    check(sky == 0x8ca6cc, "clear color in the sky");
    unsigned int sphere = px(W / 2 + 70, H / 2 + 10);
    check((sphere >> 16) > 2 * (sphere >> 8 & 255), "sphere is lit red");
    save("gl_scene");
}

int main(int argc, char **argv)
{
    static BarnixAPI api;
    api.version = BARNIX_APP_ABI;
    barnix = &api;
    if (argc > 1) out_dir = argv[1];
    for (job_order = 0; job_order < 2; job_order++) test_watertight();
    test_depth_and_lists();
    test_scene();
    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("Barnix OpenGL host test OK\n");
    return 0;
}
