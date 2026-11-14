/* Barnix OpenGL example: a lit, spinning cube. Esc quits.
 * Build: bcc glcube.c  (or put it in src/main.c of a BDK project). */
#define BARNIX_APP_NAME "glcube.elf"
#include <barnix_opengl.h>

static void face(float nx, float ny, float nz, const float v[4][3])
{
    glNormal3f(nx, ny, nz);
    for (int i = 0; i < 4; i++) glVertex3fv(v[i]);
}

int main(void)
{
    if (bxgl_open(0, 0)) return 1;
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_COLOR_MATERIAL);
    glClearColor(0.1f, 0.12f, 0.2f, 1);

    static const float sides[6][4][3] = {
        {{-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}}, {{1, -1, -1}, {-1, -1, -1}, {-1, 1, -1}, {1, 1, -1}},
        {{1, -1, 1}, {1, -1, -1}, {1, 1, -1}, {1, 1, 1}}, {{-1, -1, -1}, {-1, -1, 1}, {-1, 1, 1}, {-1, 1, -1}},
        {{-1, 1, 1}, {1, 1, 1}, {1, 1, -1}, {-1, 1, -1}}, {{-1, -1, -1}, {1, -1, -1}, {1, -1, 1}, {-1, -1, 1}}};
    static const float normals[6][3] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    static const float colors[6][3] = {{1, 0.3f, 0.3f}, {0.3f, 1, 0.3f}, {0.3f, 0.3f, 1}, {1, 1, 0.3f}, {1, 0.3f, 1}, {0.3f, 1, 1}};

    for (float angle = 0;; angle += 1.0f) {
        BgxEvent event;
        while (bgx_poll(&event))
            if (event.type == BGX_PRESS && event.code == BGX_KEY_ESC) { bxgl_close(); return 0; }
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        gluPerspective(60, (double)bxgl_width() / bxgl_height(), 0.5, 50);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        gluLookAt(0, 2, 5, 0, 0, 0, 0, 1, 0);
        GLfloat light[] = {2, 4, 3, 1};
        glLightfv(GL_LIGHT0, GL_POSITION, light);
        glRotatef(angle, 0, 1, 0);
        glRotatef(angle * 0.6f, 1, 0, 0);
        glBegin(GL_QUADS);
        for (int i = 0; i < 6; i++) {
            glColor3fv(colors[i]);
            face(normals[i][0], normals[i][1], normals[i][2], sides[i]);
        }
        glEnd();
        bxgl_swap();
    }
}
