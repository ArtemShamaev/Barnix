/* gldemo: Barnix OpenGL showcase. A textured cube, a torus from a display
 * list and a sphere under two moving colored lights, with fog over a
 * mipmapped floor. Mouse drag orbits, wheel zooms; W wireframe, L lights,
 * F fog, T textures, Space pause, Esc quits. */
#include "command.h"
#include <barnix_opengl.h>

static unsigned char bricks[64 * 64 * 3], tiles[128 * 128 * 3];

static void make_textures(void)
{
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) {
            int row = y / 8, shift = row & 1 ? 8 : 0;
            int mortar = y % 8 == 0 || (x + shift) % 16 == 0;
            int noise = (x * 37 + y * 91 + (x * y) % 13) % 24;
            unsigned char *p = bricks + (y * 64 + x) * 3;
            p[0] = (unsigned char)(mortar ? 200 : 150 + noise);
            p[1] = (unsigned char)(mortar ? 195 : 60 + noise / 2);
            p[2] = (unsigned char)(mortar ? 185 : 45 + noise / 3);
        }
    for (int y = 0; y < 128; y++)
        for (int x = 0; x < 128; x++) {
            int on = ((x / 16) ^ (y / 16)) & 1, edge = x % 16 == 0 || y % 16 == 0;
            unsigned char *p = tiles + (y * 128 + x) * 3;
            unsigned char v = (unsigned char)(edge ? 60 : on ? 220 : 120);
            p[0] = v; p[1] = v; p[2] = (unsigned char)(v + (on ? 0 : 20));
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

static void torus(float major, float minor, int rings, int sides)
{
    for (int i = 0; i < rings; i++) {
        glBegin(GL_QUAD_STRIP);
        for (int j = 0; j <= sides; j++)
            for (int k = 1; k >= 0; k--) {
                float a = (float)(2 * M_PI * (i + k) / rings), b = (float)(2 * M_PI * j / sides);
                float ca = cosf(a), sa = sinf(a), cb = cosf(b), sb = sinf(b);
                glNormal3f(ca * cb, sa * cb, sb);
                glVertex3f(ca * (major + minor * cb), sa * (major + minor * cb), minor * sb);
            }
        glEnd();
    }
}

static void light(GLenum id, float x, float y, float z, float r, float g, float b)
{
    GLfloat position[] = {x, y, z, 1}, color[] = {r, g, b, 1}, ambient[] = {r * 0.1f, g * 0.1f, b * 0.1f, 1};
    glLightfv(id, GL_POSITION, position);
    glLightfv(id, GL_DIFFUSE, color);
    glLightfv(id, GL_SPECULAR, color);
    glLightfv(id, GL_AMBIENT, ambient);
    glLightf(id, GL_QUADRATIC_ATTENUATION, 0.01f);
}

static void marker(float x, float y, float z, float r, float g, float b, GLUquadric *q)
{
    glDisable(GL_LIGHTING);
    glColor3f(r, g, b);
    glPushMatrix(); glTranslatef(x, y, z); gluSphere(q, 0.12, 8, 6); glPopMatrix();
    glEnable(GL_LIGHTING);
}

int main(int argc, const char *const *argv)
{
    (void)argv;
    if (argc > 1) return fail("gldemo - Barnix OpenGL demo; no arguments");
    if (bxgl_open(0, 0)) return fail("gldemo: this display has no graphics mode");
    int width = bxgl_width(), height = bxgl_height();
    make_textures();

    GLuint tex[2];
    glGenTextures(2, tex);
    glBindTexture(GL_TEXTURE_2D, tex[0]);
    gluBuild2DMipmaps(GL_TEXTURE_2D, GL_RGB, 64, 64, GL_RGB, GL_UNSIGNED_BYTE, bricks);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
    glBindTexture(GL_TEXTURE_2D, tex[1]);
    gluBuild2DMipmaps(GL_TEXTURE_2D, GL_RGB, 128, 128, GL_RGB, GL_UNSIGNED_BYTE, tiles);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);

    GLuint ring = glGenLists(1);
    glNewList(ring, GL_COMPILE);
    torus(1.0f, 0.35f, 32, 16);
    glEndList();
    GLUquadric *q = gluNewQuadric();

    GLfloat sky[] = {0.07f, 0.08f, 0.14f, 1}, white[] = {1, 1, 1, 1};
    glClearColor(sky[0], sky[1], sky[2], 1);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0); glEnable(GL_LIGHT1);
    glEnable(GL_COLOR_MATERIAL);
    glMaterialfv(GL_FRONT, GL_SPECULAR, white);
    glMaterialf(GL_FRONT, GL_SHININESS, 32);
    GLfloat ambient[] = {0.15f, 0.15f, 0.18f, 1};
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, ambient);
    glFogi(GL_FOG_MODE, GL_LINEAR); glFogf(GL_FOG_START, 8); glFogf(GL_FOG_END, 28); glFogfv(GL_FOG_COLOR, sky);
    glEnable(GL_FOG);

    float yaw = 30, pitch = 20, distance = 9, time = 0;
    int wire = 0, lights = 1, fog = 1, textures = 1, paused = 0;
    unsigned int last = bgx_millis();
    for (;;) {
        BgxEvent event;
        while (bgx_poll(&event)) {
            if (event.type != BGX_PRESS) continue;
            switch (event.code) {
            case BGX_KEY_ESC: case BGX_KEY_Q: goto done;
            case BGX_KEY_W: wire = !wire; break;
            case 0x26: lights = !lights; break;                     /* L */
            case BGX_KEY_F: fog = !fog; break;
            case 0x14: textures = !textures; break;                 /* T */
            case BGX_KEY_SPACE: paused = !paused; break;
            }
        }
        BgxMouse mouse;
        bgx_mouse(&mouse);
        if (mouse.buttons & 1) { yaw += mouse.dx * 0.4f; pitch += mouse.dy * 0.4f; }
        if (pitch < -10) pitch = -10;
        if (pitch > 85) pitch = 85;
        distance -= mouse.wheel * 0.7f;
        if (distance < 4) distance = 4;
        if (distance > 20) distance = 20;
        unsigned int now = bgx_millis();
        if (!paused) time += (now - last) * 0.001f;
        last = now;

        glViewport(0, 0, width, height);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glMatrixMode(GL_PROJECTION); glLoadIdentity();
        gluPerspective(60, (double)width / height, 0.3, 60);
        glMatrixMode(GL_MODELVIEW); glLoadIdentity();
        glTranslatef(0, 0, -distance);
        glRotatef(pitch, 1, 0, 0);
        glRotatef(yaw, 0, 1, 0);
        if (lights) glEnable(GL_LIGHTING); else glDisable(GL_LIGHTING);
        if (fog) glEnable(GL_FOG); else glDisable(GL_FOG);
        glPolygonMode(GL_FRONT_AND_BACK, wire ? GL_LINE : GL_FILL);

        float l0x = cosf(time * 0.9f) * 4, l0z = sinf(time * 0.9f) * 4, l1x = cosf(-time * 0.6f + 2) * 3.5f, l1z = sinf(-time * 0.6f + 2) * 3.5f;
        light(GL_LIGHT0, l0x, 2.5f, l0z, 1.0f, 0.85f, 0.6f);
        light(GL_LIGHT1, l1x, 1.2f, l1z, 0.35f, 0.55f, 1.0f);

        if (textures) glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, tex[1]);
        glColor3f(1, 1, 1);
        glNormal3f(0, 1, 0);
        for (int z = -6; z < 6; z++) {   /* tessellated, so per-vertex lighting shows the pools of light */
            glBegin(GL_QUAD_STRIP);
            for (int x = -6; x <= 6; x++) {
                glTexCoord2f(x * 0.5f, z * 0.5f); glVertex3f(x * 2.0f, -1.5f, z * 2.0f);
                glTexCoord2f(x * 0.5f, (z + 1) * 0.5f); glVertex3f(x * 2.0f, -1.5f, (z + 1) * 2.0f);
            }
            glEnd();
        }
        glBindTexture(GL_TEXTURE_2D, tex[0]);
        glPushMatrix();
        glTranslatef(-2.4f, 0, 0);
        glRotatef(time * 40, 0, 1, 0); glRotatef(time * 25, 1, 0, 0);
        cube();
        glPopMatrix();
        glDisable(GL_TEXTURE_2D);

        glPushMatrix();
        glTranslatef(2.4f, 0.2f, 0);
        glRotatef(time * 60, 1, 0.3f, 0);
        glColor3f(0.95f, 0.75f, 0.2f);
        glCallList(ring);
        glPopMatrix();

        glPushMatrix();
        glTranslatef(0, 0.3f + sinf(time * 2) * 0.4f, -2.6f);
        glColor3f(0.3f, 0.8f, 0.5f);
        gluSphere(q, 0.9, 24, 16);
        glPopMatrix();

        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        if (lights) {
            marker(l0x, 2.5f, l0z, 1, 0.85f, 0.6f, q);
            marker(l1x, 1.2f, l1z, 0.35f, 0.55f, 1, q);
        }

        glFinish();
        BgxCanvas *screen = bxgl_canvas();
        char line[96];
        const char *prefix = "Barnix OpenGL  fps ";
        int n = 0;
        while (prefix[n]) { line[n] = prefix[n]; n++; }
        int fps = bxgl_fps(), digits = fps >= 100 ? 3 : fps >= 10 ? 2 : 1;
        for (int d = digits - 1; d >= 0; d--, fps /= 10) line[n + d] = (char)('0' + fps % 10);
        line[n + digits] = 0;
        bgx_text_shadow(screen, 8, 6, line, 0xffffff, 1);
        bgx_text_shadow(screen, 8, 22, (const char *)glGetString(GL_RENDERER), 0xa0c0ff, 1);
        bgx_text_shadow(screen, 8, height - 20, "Drag/wheel: camera  W wire  L light  F fog  T tex  Esc", 0xc0c0c0, 1);
        bxgl_swap();
    }
done:
    gluDeleteQuadric(q);
    bxgl_close();
    barnix->clear();
    return 0;
}
