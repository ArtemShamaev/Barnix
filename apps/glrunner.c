/* GL Runner: an endless three-track city runner for Barnix OpenGL. */
#include "command.h"
#include <barnix_opengl.h>
#include <barnix_sound.h>

#define ROWS 16
#define ROW_GAP 9.0f
#define PLAYER_Z 4.0f
#define KEY_P 0x19
#define KEY_H 0x23
enum { EMPTY, TRAIN, BARRIER, SIGN, MAGNET, SHIELD };
enum { TITLE, PLAYING, PAUSED, GAME_OVER };
typedef struct {
    float z;
    unsigned char obstacle[3], hit[3], coins, safe;
} Row;
static Row rows[ROWS];
static unsigned int random_state = 0x729ab31u;
static int state, lane, coins, best, shield_stocks, sound_ready;
static float x_player, jump_y, jump_v, slide_time, slide_pose, landing, magnet_time, shield_time;
static float distance_run, speed, animation, invulnerable;
static short effects[3][2400];
static unsigned int effect_frames[3];
static char score_path[160];
enum { TEX_BALLAST, TEX_FACADE, TEX_TRAIN, TEX_COUNT };
static GLuint textures[TEX_COUNT];
static unsigned char texture_pixels[TEX_COUNT][64 * 64 * 3];
typedef struct { float x, y, z, vx, vy, vz, life; unsigned char crash; } Particle;
static Particle particles[40];
static int next_particle;
static float shake_time, shake_phase;

static unsigned int pixel_noise(int x, int y)
{
    unsigned int v = (unsigned int)x * 0x9e3779b9u ^ (unsigned int)y * 0x85ebca6bu;
    v ^= v >> 16; v *= 0x7feb352du; v ^= v >> 15;
    return v;
}

static void make_textures(void)
{
    for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) {
        int p = (y * 64 + x) * 3;
        unsigned int grit = pixel_noise(x, y);
        int pebble = (int)(grit & 31) - 15;
        int stone = (int)(pixel_noise(x / 3, y / 3) & 15);
        unsigned char *ballast = texture_pixels[TEX_BALLAST] + p;
        ballast[0] = (unsigned char)(58 + pebble + stone);
        ballast[1] = (unsigned char)(62 + pebble + stone);
        ballast[2] = (unsigned char)(71 + pebble + stone);

        int mortar = x % 16 == 0 || y % 16 == 0;
        int wx = x % 16, wy = y % 16;
        int window = wx >= 4 && wx <= 12 && wy >= 4 && wy <= 11;
        int lit = pixel_noise(x / 16, y / 16) % 4 != 0;
        unsigned char *facade = texture_pixels[TEX_FACADE] + p;
        if (window) {
            facade[0] = (unsigned char)(lit ? 183 : 55);
            facade[1] = (unsigned char)(lit ? 186 : 87);
            facade[2] = (unsigned char)(lit ? 141 : 121);
        } else if (mortar) {
            facade[0] = 96; facade[1] = 99; facade[2] = 108;
        } else {
            facade[0] = (unsigned char)(173 + (grit & 7));
            facade[1] = (unsigned char)(179 + (grit & 7));
            facade[2] = (unsigned char)(192 + (grit & 7));
        }

        int panel = y % 32, seam = x % 32 == 0;
        int train_window = panel >= 8 && panel <= 20 && x % 32 >= 3 && x % 32 <= 28;
        unsigned char *train = texture_pixels[TEX_TRAIN] + p;
        if (train_window) {
            train[0] = 43; train[1] = 104; train[2] = 137;
        } else if (panel >= 24 && panel <= 27) {
            train[0] = 239; train[1] = 209; train[2] = 97;
        } else if (seam) {
            train[0] = 93; train[1] = 112; train[2] = 126;
        } else {
            train[0] = (unsigned char)(207 + (grit & 7));
            train[1] = (unsigned char)(214 + (grit & 7));
            train[2] = (unsigned char)(222 + (grit & 7));
        }
    }
    glGenTextures(TEX_COUNT, textures);
    for (int i = 0; i < TEX_COUNT; i++) {
        glBindTexture(GL_TEXTURE_2D, textures[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 64, 64, 0, GL_RGB, GL_UNSIGNED_BYTE,
                     texture_pixels[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    }
}

static void best_path(void)
{
    BarnixDirEntry entry; unsigned int cursor = 0;
    const char *user = barnix->current_user();
    int at = 0;
    const char *prefix = "/home/";
    while (*prefix) score_path[at++] = *prefix++;
    while (*user && at < 125) score_path[at++] = *user++;
    score_path[at] = 0;
    if (barnix->directory(score_path, &cursor, &entry) < 0) at = 0;
    const char *suffix = at ? "/.glrunner.score" : "/glrunner.score";
    while (*suffix && at < (int)sizeof(score_path) - 1) score_path[at++] = *suffix++;
    score_path[at] = 0;
}

static void load_best(void)
{
    char data[16];
    int n = barnix->read(score_path, 0, data, sizeof(data) - 1);
    if (n <= 0) return;
    data[n] = 0;
    unsigned int value = 0;
    for (int i = 0; i < n && data[i] >= '0' && data[i] <= '9'; i++) {
        if (value > 10000000) return;
        value = value * 10 + (unsigned int)(data[i] - '0');
    }
    best = (int)value;
}

static void save_best(void)
{
    char reverse[12], data[12]; int count = 0;
    unsigned int value = (unsigned int)best;
    do { reverse[count++] = (char)('0' + value % 10); value /= 10; } while (value && count < 11);
    for (int i = 0; i < count; i++) data[i] = reverse[count - 1 - i];
    data[count++] = '\n';
    barnix->write(score_path, data, count);
}

static unsigned int random_number(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

static void burst(float x, float y, float z, int crash)
{
    for (int i = 0; i < (crash ? 12 : 5); i++) {
        Particle *p = &particles[next_particle++ % 40];
        p->x = x; p->y = y; p->z = z;
        p->vx = ((int)(random_number() % 201) - 100) * 0.018f;
        p->vy = (crash ? 1.5f : 2.0f) + (random_number() % 100) * 0.015f;
        p->vz = ((int)(random_number() % 201) - 100) * 0.018f;
        p->life = crash ? 0.7f : 0.42f;
        p->crash = (unsigned char)crash;
    }
}

static void update_particles(float dt)
{
    if (shake_time > 0) { shake_time -= dt; if (shake_time < 0) shake_time = 0; }
    shake_phase += dt;
    for (int i = 0; i < 40; i++) {
        Particle *p = &particles[i];
        if (p->life <= 0) continue;
        p->life -= dt;
        p->x += p->vx * dt;
        p->y += p->vy * dt;
        p->z += p->vz * dt;
        p->vy -= 8 * dt;
    }
}

static void sound(int kind)
{
    if (sound_ready) bsnd_play(effects[kind], effect_frames[kind], 12000, 180, 0);
}

static void init_sound(void)
{
    if (bsnd_device(0, 0) != BARNIX_SOUND_CARD) return;
    const BsndTone tones[3] = {
        {BSND_SINE, 740, 1180, 95, 2, 45, 210},
        {BSND_TRIANGLE, 420, 570, 110, 3, 55, 180},
        {BSND_SAW, 190, 75, 180, 2, 120, 180}
    };
    for (int i = 0; i < 3; i++) effect_frames[i] = bsnd_synth(effects[i], 2400, 12000, &tones[i]);
    sound_ready = 1;
}

static void make_row(Row *row, float z, int first)
{
    row->z = z;
    row->safe = (unsigned char)(random_number() % 3);
    row->coins = 0;
    for (int lane_index = 0; lane_index < 3; lane_index++) {
        row->hit[lane_index] = 0;
        row->obstacle[lane_index] = EMPTY;
        if (lane_index == row->safe) continue;
        if (!first && random_number() % 100 < 62)
            row->obstacle[lane_index] = (unsigned char)(TRAIN + random_number() % 3);
    }
    if (!first) {
        unsigned int prize = random_number() % 28;
        if (prize == 0) row->obstacle[row->safe] = MAGNET;
        if (prize == 1) row->obstacle[row->safe] = SHIELD;
    }
}

static void new_run(void)
{
    state = PLAYING; lane = 1; coins = 0; shield_stocks = 0;
    x_player = 0; jump_y = 0; jump_v = 0; slide_time = 0; slide_pose = 0; landing = 0;
    magnet_time = 0; shield_time = 0; invulnerable = 0;
    distance_run = 0; speed = 12; animation = 0;
    for (int i = 0; i < 40; i++) particles[i].life = 0;
    shake_time = 0;
    for (int i = 0; i < ROWS; i++) make_row(&rows[i], -22 - i * ROW_GAP, i < 2);
}

static void hit_obstacle(void)
{
    if (state != PLAYING || invulnerable > 0) return;
    burst(x_player, 1.0f + jump_y, PLAYER_Z, 1);
    shake_time = 0.32f;
    if (shield_time > 0) {
        shield_time = 0;
        invulnerable = 1.5f;
        sound(2);
        return;
    }
    state = GAME_OVER;
    int points = (int)distance_run + coins * 25;
    if (points > best) { best = points; save_best(); }
    sound(2);
}

static void update(float dt)
{
    if (state != PLAYING) return;
    speed = 12 + distance_run / 90;
    if (speed > 27) speed = 27;
    distance_run += speed * dt;
    animation += dt;
    if (magnet_time > 0) magnet_time -= dt;
    if (shield_time > 0) shield_time -= dt;
    if (invulnerable > 0) invulnerable -= dt;
    if (slide_time > 0) slide_time -= dt;
    float pose_target = slide_time > 0 ? 1 : 0;
    if (slide_pose < pose_target) { slide_pose += dt * 9; if (slide_pose > 1) slide_pose = 1; }
    if (slide_pose > pose_target) { slide_pose -= dt * 9; if (slide_pose < 0) slide_pose = 0; }
    if (landing > 0) { landing -= dt; if (landing < 0) landing = 0; }
    if (jump_y > 0 || jump_v > 0) {
        jump_y += jump_v * dt;
        jump_v -= 18 * dt;
        if (jump_y < 0) { jump_y = 0; jump_v = 0; landing = 0.16f; }
    }
    float target = (lane - 1) * 2.5f;
    float step = dt * 15;
    if (x_player < target - step) x_player += step;
    else if (x_player > target + step) x_player -= step;
    else x_player = target;
    for (int i = 0; i < ROWS; i++) {
        Row *row = &rows[i];
        row->z += speed * dt;
        for (int track = 0; track < 3; track++) {
            int kind = row->obstacle[track];
            float dx = x_player - (track - 1) * 2.5f;
            if (dx < 0) dx = -dx;
            if (dx > 0.8f || row->hit[track]) continue;
            float reach = kind == TRAIN ? 2.7f : 0.85f;
            if (row->z < PLAYER_Z - reach || row->z > PLAYER_Z + reach) continue;
            if (kind == TRAIN || (kind == BARRIER && jump_y < 0.95f) ||
                (kind == SIGN && slide_time <= 0)) {
                row->hit[track] = 1;
                hit_obstacle();
            } else if (kind == MAGNET) {
                row->hit[track] = 1; magnet_time = 12; sound(0);
            } else if (kind == SHIELD) {
                row->hit[track] = 1; shield_time = 12; sound(0);
            }
        }
        for (int c = 0; c < 3; c++) {
            if (row->coins & (1 << c)) continue;
            float coin_z = row->z - 2.0f - c * 1.45f;
            if (coin_z < PLAYER_Z - 0.8f || coin_z > PLAYER_Z + 0.8f) continue;
            float dx = x_player - (row->safe - 1) * 2.5f;
            if (dx < 0) dx = -dx;
            if (dx < 0.85f || magnet_time > 0) {
                row->coins |= 1 << c;
                coins++;
                if (coins % 25 == 0) shield_stocks++;
                burst((row->safe - 1) * 2.5f, 1.15f, PLAYER_Z, 0);
                sound(0);
            }
        }
        if (row->z > 11) make_row(row, row->z - ROWS * ROW_GAP, 0);
    }
}

static void box(void)
{
    static const float normals[6][3] = {{0,0,1},{0,0,-1},{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}};
    static const float vertices[6][4][3] = {
        {{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}},
        {{1,-1,-1},{-1,-1,-1},{-1,1,-1},{1,1,-1}},
        {{1,-1,1},{1,-1,-1},{1,1,-1},{1,1,1}},
        {{-1,-1,-1},{-1,-1,1},{-1,1,1},{-1,1,-1}},
        {{-1,1,1},{1,1,1},{1,1,-1},{-1,1,-1}},
        {{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1}}
    };
    static const float uv[4][2] = {{0,0},{1,0},{1,1},{0,1}};
    glBegin(GL_QUADS);
    for (int face = 0; face < 6; face++) {
        glNormal3fv(normals[face]);
        for (int vertex = 0; vertex < 4; vertex++) {
            glTexCoord2fv(uv[vertex]);
            glVertex3fv(vertices[face][vertex]);
        }
    }
    glEnd();
}

static void draw_box(float x, float y, float z, float sx, float sy, float sz,
                     float red, float green, float blue, float angle)
{
    glColor3f(red, green, blue);
    glPushMatrix();
    glTranslatef(x, y, z);
    if (angle) glRotatef(angle, 0, 1, 0);
    glScalef(sx, sy, sz);
    box();
    glPopMatrix();
}

static void textured_box(int texture, float x, float y, float z, float sx, float sy, float sz,
                         float red, float green, float blue)
{
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, textures[texture]);
    draw_box(x, y, z, sx, sy, sz, red, green, blue, 0);
    glDisable(GL_TEXTURE_2D);
}

static void coin(float x, float z, float phase)
{
    glDisable(GL_LIGHTING);
    draw_box(x, 1.15f + 0.1f * sinf(phase), z, 0.34f, 0.34f, 0.08f,
             1, 0.72f, 0.08f, phase * 110);
    draw_box(x, 1.15f + 0.1f * sinf(phase), z, 0.24f, 0.24f, 0.09f,
             1, 0.95f, 0.38f, phase * 110);
    glEnable(GL_LIGHTING);
}

static void draw_train(float x, float z, int variant)
{
    float red = variant ? 0.18f : 0.85f, green = variant ? 0.65f : 0.19f;
    textured_box(TEX_TRAIN, x, 1.55f, z, 1.02f, 1.42f, 3.1f, red, green, 0.72f);
    draw_box(x, 2.65f, z, 0.85f, 0.13f, 2.9f, 0.84f, 0.87f, 0.82f, 0);
    draw_box(x, 1.95f, z + 3.12f, 0.8f, 0.45f, 0.035f, 0.15f, 0.35f, 0.5f, 0);
    draw_box(x, 0.65f, z + 3.13f, 0.6f, 0.12f, 0.04f, 0.95f, 0.85f, 0.35f, 0);
    for (int s = -1; s <= 1; s += 2) {
        draw_box(x + s * 0.62f, 0.7f, z + 3.14f, 0.15f, 0.15f, 0.035f,
                 1, 0.18f, 0.08f, 0);
        for (int w = -1; w <= 1; w++)
            draw_box(x + s * 1.04f, 1.9f, z + w * 1.8f, 0.035f, 0.43f, 0.65f,
                     0.13f, 0.37f, 0.53f, 0);
    }
}

static void draw_obstacle(int kind, float x, float z, int variant)
{
    if (kind == TRAIN) draw_train(x, z, variant);
    else if (kind == BARRIER) {
        draw_box(x, 0.52f, z, 0.94f, 0.53f, 0.18f, 0.94f, 0.38f, 0.07f, 0);
        draw_box(x, 0.78f, z + 0.2f, 0.84f, 0.13f, 0.03f, 1, 0.93f, 0.67f, 0);
        draw_box(x, 0.25f, z + 0.2f, 0.84f, 0.13f, 0.03f, 1, 0.93f, 0.67f, 0);
    } else if (kind == SIGN) {
        for (int s = -1; s <= 1; s += 2)
            draw_box(x + s * 0.9f, 1.12f, z, 0.09f, 1.1f, 0.09f, 0.9f, 0.72f, 0.17f, 0);
        draw_box(x, 1.94f, z, 1.05f, 0.32f, 0.15f, 0.96f, 0.73f, 0.14f, 0);
        draw_box(x, 1.94f, z + 0.17f, 0.65f, 0.08f, 0.03f, 0.14f, 0.18f, 0.22f, 0);
    } else if (kind == MAGNET || kind == SHIELD) {
        draw_box(x, 1.05f, z, 0.43f, 0.43f, 0.43f, kind == MAGNET ? 0.2f : 0.3f,
                 kind == MAGNET ? 0.8f : 1, 1, animation * 100);
    }
}

static void draw_city(void)
{
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, textures[TEX_BALLAST]);
    glColor3f(0.78f, 0.8f, 0.83f);
    glNormal3f(0, 1, 0);
    for (int i = 0; i < 12; i++) {
        float near_z = 10 - i * 7.5f, far_z = near_z - 7.5f;
        glBegin(GL_QUADS);
        glTexCoord2f(0, near_z * 0.35f); glVertex3f(-4.35f, -0.53f, near_z);
        glTexCoord2f(6, near_z * 0.35f); glVertex3f(4.35f, -0.53f, near_z);
        glTexCoord2f(6, far_z * 0.35f); glVertex3f(4.35f, -0.53f, far_z);
        glTexCoord2f(0, far_z * 0.35f); glVertex3f(-4.35f, -0.53f, far_z);
        glEnd();
    }
    glDisable(GL_TEXTURE_2D);
    for (int track = -1; track <= 1; track++) {
        float x = track * 2.5f;
        for (int side = -1; side <= 1; side += 2)
            draw_box(x + side * 0.82f, -0.44f, -33, 0.055f, 0.11f, 43,
                     0.7f, 0.74f, 0.78f, 0);
        for (int i = 0; i < 14; i++) {
            float z = 6 - i * 5 + (float)((int)distance_run % 5);
            draw_box(x, -0.52f, z, 1.04f, 0.06f, 0.12f, 0.34f, 0.24f, 0.16f, 0);
        }
    }
    for (int side = -1; side <= 1; side += 2) {
        draw_box(side * 4.8f, -0.3f, -33, 0.34f, 0.55f, 43, 0.52f, 0.43f, 0.35f, 0);
        for (int i = 0; i < 9; i++) {
            float z = 4 - i * 8 + (float)((int)distance_run % 8);
            int height = 4 + (i * 7 + side * 3 + 30) % 5;
            float r = side > 0 ? 0.38f : 0.45f;
            textured_box(TEX_FACADE, side * 8.1f, height * 0.5f - 0.6f, z,
                         2.4f, height * 0.5f, 3.0f,
                         r + 0.3f, 0.68f + (i % 3) * 0.07f, 0.78f);
            draw_box(side * 8.1f, height - 0.08f, z, 2.52f, 0.16f, 3.12f,
                     0.26f, 0.3f, 0.36f, 0);
        }
    }
}

static void arm(int side, float angle)
{
    glPushMatrix();
    glTranslatef(side * 0.48f, 1.12f, 0);
    glRotatef(angle, 1, 0, 0);
    draw_box(0, -0.21f, 0, 0.13f, 0.23f, 0.15f, 0.94f, 0.62f, 0.43f, 0);
    glTranslatef(0, -0.43f, 0);
    glRotatef(jump_y > 0 ? -30 : 16, 1, 0, 0);
    draw_box(0, -0.14f, 0, 0.12f, 0.16f, 0.14f, 0.94f, 0.62f, 0.43f, 0);
    glPopMatrix();
}

static void leg(int side, float angle)
{
    glPushMatrix();
    glTranslatef(side * 0.21f, 0.41f, 0);
    glRotatef(angle, 1, 0, 0);
    draw_box(0, -0.22f, 0, 0.16f, 0.23f, 0.18f, 0.18f, 0.25f, 0.68f, 0);
    glTranslatef(0, -0.43f, 0);
    glRotatef(jump_y > 0 ? 40 : angle < 0 ? 28 : 5, 1, 0, 0);
    draw_box(0, -0.21f, 0, 0.14f, 0.22f, 0.15f, 0.16f, 0.22f, 0.61f, 0);
    draw_box(0, -0.43f, 0.12f, 0.18f, 0.09f, 0.28f, 0.95f, 0.95f, 0.9f, 0);
    glPopMatrix();
}

static void draw_runner(void)
{
    float bob = jump_y > 0 || slide_pose > 0.05f ? 0 : 0.045f * sinf(animation * 17);
    float shadow_width = 0.58f - jump_y * 0.1f;
    if (shadow_width < 0.26f) shadow_width = 0.26f;
    draw_box(x_player, -0.505f, PLAYER_Z, shadow_width, 0.012f, 0.42f,
             0.08f, 0.1f, 0.13f, 0);
    if (shield_time > 0 || invulnerable > 0) {
        glDisable(GL_LIGHTING);
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        draw_box(x_player, jump_y + 1.02f, PLAYER_Z, 0.88f, 1.01f, 0.72f,
                 shield_time > 0 ? 0.18f : 1, 0.85f, 1, animation * 90);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glEnable(GL_LIGHTING);
    }
    glPushMatrix();
    glTranslatef(x_player, jump_y + bob + 0.4f, PLAYER_Z);
    float lean = ((lane - 1) * 2.5f - x_player) * -7.0f;
    if (lean > 13) lean = 13;
    if (lean < -13) lean = -13;
    glRotatef(state == GAME_OVER ? 68 : lean, 0, 0, 1);
    glRotatef(-67 * slide_pose, 1, 0, 0);
    glTranslatef(0, -0.4f, 0);
    if (landing > 0) glScalef(1.0f, 1.0f - landing, 1.0f);
    float stride = jump_y > 0 ? 20 : sinf(animation * 15) * 31;
    leg(-1, stride); leg(1, -stride);
    draw_box(0, 0.83f, 0, 0.4f, 0.46f, 0.27f, 0.12f, 0.65f, 0.86f, 0);
    draw_box(0, 0.83f, 0.29f, 0.3f, 0.34f, 0.12f, 0.96f, 0.42f, 0.18f, 0);
    draw_box(0, 1.15f, 0.02f, 0.41f, 0.07f, 0.3f, 0.96f, 0.8f, 0.25f, 0);
    arm(-1, jump_y > 0 ? -145 : -stride * 0.8f);
    arm(1, jump_y > 0 ? -145 : stride * 0.8f);
    draw_box(0, 1.53f, 0, 0.3f, 0.33f, 0.28f, 0.94f, 0.65f, 0.43f, 0);
    draw_box(0, 1.81f, 0, 0.34f, 0.12f, 0.33f, 0.92f, 0.18f, 0.23f, 0);
    draw_box(0, 1.72f, 0.31f, 0.4f, 0.035f, 0.18f, 0.92f, 0.18f, 0.23f, 0);
    glPopMatrix();
}

static void draw_particles(void)
{
    glDisable(GL_LIGHTING);
    for (int i = 0; i < 40; i++) {
        Particle *p = &particles[i];
        if (p->life <= 0) continue;
        float size = p->life * (p->crash ? 0.18f : 0.13f);
        draw_box(p->x, p->y, p->z, size, size, size,
                 1, p->crash ? 0.22f : 0.83f, p->crash ? 0.08f : 0.14f, 0);
    }
    glEnable(GL_LIGHTING);
}

static void append(char *buffer, int *at, const char *word)
{
    while (*word && *at < 119) buffer[(*at)++] = *word++;
    buffer[*at] = 0;
}

static void append_number(char *buffer, int *at, unsigned int value)
{
    char digits[12]; int count = 0;
    do { digits[count++] = (char)('0' + value % 10); value /= 10; } while (value && count < 11);
    while (count && *at < 119) buffer[(*at)++] = digits[--count];
    buffer[*at] = 0;
}

static void centered(BgxCanvas *screen, int y, const char *message, unsigned int color)
{
    bgx_text_shadow(screen, (screen->width - bgx_text_width(message, 1)) / 2, y, message, color, 1);
}

static void sky(BgxCanvas *screen)
{
    int horizon = screen->height * 4 / 5;
    for (int y = 0; y < horizon; y++) {
        int red = 39 + y * 75 / horizon;
        int green = 81 + y * 92 / horizon;
        int blue = 137 + y * 52 / horizon;
        bgx_fill(screen, 0, y, screen->width, 1, BGX_RGB(red, green, blue));
    }
}

static void hud(BgxCanvas *screen)
{
    char line[120] = {0}; int at = 0;
    append(line, &at, "SCORE "); append_number(line, &at, (unsigned int)distance_run + coins * 25);
    append(line, &at, "   BEST "); append_number(line, &at, (unsigned int)best);
    bgx_text_shadow(screen, 10, 8, line, 0xffffff, 1);
    at = 0; line[0] = 0;
    append(line, &at, "COINS "); append_number(line, &at, (unsigned int)coins);
    append(line, &at, "   SPEED "); append_number(line, &at, (unsigned int)speed);
    bgx_text_shadow(screen, 10, 25, line, 0xffdb5b, 1);
    if (magnet_time > 0) bgx_text_shadow(screen, 10, 43, "MAGNET", 0x59e4ff, 1);
    if (shield_time > 0) bgx_text_shadow(screen, 82, 43, "SHIELD", 0x82ffcb, 1);
    if (shield_stocks) bgx_text_shadow(screen, 10, screen->height - 39, "H: hoverboard", 0x82ffcb, 1);
    bgx_text_shadow(screen, 10, screen->height - 21, "A/D lane  W jump  S slide  P pause", 0xe0ecff, 1);
    if (state == TITLE) {
        centered(screen, screen->height / 2 - 27, "GL RUNNER: METRO DASH", 0xffd25a);
        centered(screen, screen->height / 2 - 6, "Three tracks. Endless chase.", 0xffffff);
        centered(screen, screen->height / 2 + 16, "SPACE / ENTER to start", 0xffffff);
    } else if (state == PAUSED) {
        centered(screen, screen->height / 2 - 8, "PAUSED  -  P to continue", 0xffffff);
    } else if (state == GAME_OVER) {
        centered(screen, screen->height / 2 - 18, "CRASH! GAME OVER", 0xff6969);
        centered(screen, screen->height / 2 + 7, "SPACE / R to try again", 0xffffff);
    }
}

static void swipe(int dx, int dy)
{
    int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    if (ax < 24 && ay < 24) return;
    if (ax > ay) {
        if (dx < 0 && lane > 0) lane--;
        if (dx > 0 && lane < 2) lane++;
    } else if (dy < 0 && jump_y == 0 && slide_time <= 0) {
        jump_v = 7.4f; sound(1);
    } else if (dy > 0 && jump_y == 0) slide_time = 0.9f;
}

int main(int argc, const char *const *argv)
{
    (void)argv;
    if (argc > 1) return fail("glrunner - Barnix OpenGL endless runner; no arguments");
    /* Software rasterization stays responsive on BIOS VGA and QEMU. */
    int display_w = 0, display_h = 0;
    if (bgx_display(&display_w, &display_h)) return fail("glrunner: this display has no graphics mode");
    if (bxgl_open(display_w >= 400 && display_h >= 300 ? 400 : 320,
                  display_w >= 400 && display_h >= 300 ? 300 : 200))
        return fail("glrunner: this display has no graphics mode");
    int width = bxgl_width(), height = bxgl_height();
    init_sound();
    best_path(); load_best();
    glViewport(0, 0, width, height);
    glClearColor(0.27f, 0.43f, 0.59f, 1);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_COLOR_MATERIAL);
    GLfloat position[] = {-0.4f, 0.8f, 0.5f, 0};
    GLfloat sunlight[] = {0.85f, 0.85f, 0.78f, 1};
    GLfloat ambient[] = {0.58f, 0.58f, 0.64f, 1};
    GLfloat fog[] = {0.27f, 0.43f, 0.59f, 1};
    glLightfv(GL_LIGHT0, GL_POSITION, position);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, sunlight);
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, ambient);
    glFogi(GL_FOG_MODE, GL_LINEAR);
    glFogf(GL_FOG_START, 27); glFogf(GL_FOG_END, 72);
    glFogfv(GL_FOG_COLOR, fog); glEnable(GL_FOG);
    make_textures();
    new_run(); state = TITLE;
    unsigned int last = bgx_millis();
    int dragging = 0, drag_x = 0, drag_y = 0;
    for (;;) {
        BgxEvent event;
        while (bgx_poll(&event)) {
            if (event.type != BGX_PRESS || event.repeat) continue;
            if (event.code == BGX_KEY_ESC || event.code == BGX_KEY_Q) goto done;
            if (state == TITLE || state == GAME_OVER) {
                if (event.code == BGX_KEY_SPACE || event.code == BGX_KEY_ENTER || event.code == BGX_KEY_R) new_run();
                continue;
            }
            if (event.code == KEY_P) { state = state == PLAYING ? PAUSED : PLAYING; continue; }
            if (state != PLAYING) continue;
            if ((event.code == BGX_KEY_LEFT || event.code == BGX_KEY_A) && lane > 0) lane--;
            if ((event.code == BGX_KEY_RIGHT || event.code == BGX_KEY_D) && lane < 2) lane++;
            if ((event.code == BGX_KEY_UP || event.code == BGX_KEY_W || event.code == BGX_KEY_SPACE) && jump_y == 0 && slide_time <= 0) {
                jump_v = 7.4f; sound(1);
            }
            if ((event.code == BGX_KEY_DOWN || event.code == BGX_KEY_S) && jump_y == 0) slide_time = 0.9f;
            if (event.code == KEY_H && shield_stocks > 0 && shield_time <= 0) {
                shield_stocks--; shield_time = 12; sound(1);
            }
        }
        BgxMouse mouse;
        bgx_mouse(&mouse);
        if (mouse.pressed & 1) {
            if (state == TITLE || state == GAME_OVER) new_run();
            dragging = 1; drag_x = mouse.x; drag_y = mouse.y;
        }
        if ((mouse.released & 1) && dragging) {
            if (state == PLAYING) swipe(mouse.x - drag_x, mouse.y - drag_y);
            dragging = 0;
        }
        unsigned int now = bgx_millis(), elapsed = now - last;
        last = now;
        if (elapsed > 250) elapsed = 250;
        while (elapsed) {
            unsigned int step = elapsed > 20 ? 20 : elapsed;
            update(step * 0.001f);
            if (state != PAUSED) update_particles(step * 0.001f);
            elapsed -= step;
        }
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        sky(bxgl_canvas());
        glMatrixMode(GL_PROJECTION); glLoadIdentity();
        gluPerspective(66, (double)width / height, 0.3, 85);
        glMatrixMode(GL_MODELVIEW); glLoadIdentity();
        float shake = shake_time * sinf(shake_phase * 73) * 0.45f;
        gluLookAt(x_player * 0.16f + shake, 4.1 + jump_y * 0.2f + shake * 0.4f, 12,
                  x_player * 0.12f, 0.7, -12, 0, 1, 0);
        draw_city();
        for (int i = 0; i < ROWS; i++) {
            Row *row = &rows[i];
            for (int track = 0; track < 3; track++) {
                int kind = row->obstacle[track];
                if (kind && !row->hit[track] && row->z > -70 && row->z < 9)
                    draw_obstacle(kind, (track - 1) * 2.5f, row->z, i & 1);
            }
            for (int c = 0; c < 3; c++) {
                float z = row->z - 2 - c * 1.45f;
                if (!(row->coins & (1 << c)) && z > -70 && z < 9)
                    coin((row->safe - 1) * 2.5f, z, animation * 4 + c);
            }
        }
        draw_runner();
        draw_particles();
        glFinish();
        hud(bxgl_canvas());
        bxgl_swap();
    }
done:
    bsnd_stop(-1);
    bxgl_close();
    barnix->clear();
    return 0;
}
