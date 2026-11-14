/* Barnocraft game core: a voxel sandbox ported from the NASM/X11/OpenGL
 * prototype (barnocraft.asm). World generation, caves, ores, trees, lighting,
 * physics, block picking and the 36-slot inventory follow the original.
 *
 * Barnix applications are i386 code without an FPU or GPU, so the OpenGL
 * renderer became a fixed-point voxel raycaster with mipmapped textures. The
 * core draws a frame of any size into a BGX canvas; barnocraft.c runs it full
 * screen and Happy Gouse runs it in a window. */
#include "barnocraft_game.h"
#include "barnocraft_assets.h"
#include <barnix_sound.h>

#define WX 80
#define WZ 80
#define WY 32
#define WXZ (WX * WZ)
#define SEED 1337
#define CAVE_K 22
#define CAVE_K_TOP 7
#define NSLOTS 36
#define STACK 64
#define ONE 65536
#define ANGLES 4096
#define PITCH_MAX 1010              /* 1.55 rad */
#define PLAYER_W 19661              /* 0.3 */
#define PLAYER_H 117965             /* 1.8 */
#define EYE 106168                  /* 1.62 */
#define EPS 66
#define REACH (5 * ONE)
#define SLOT 40
#define SSTEP 44

enum { AIR, GRASS, DIRT, STONE, WOOD, GLASS, LEAVES, BEDROCK, COAL, IRON, NBLOCKS };
enum { TITLE, PLAY, INVENTORY, PAUSED };

static unsigned char world[WY * WXZ], hmap[WXZ], ctop[WXZ];
static unsigned int rng_state;
static const unsigned int seed = SEED;

static const unsigned char block_tiles[NBLOCKS][3] = {
    {0,0,0}, {0,1,2}, {2,2,2}, {3,3,3}, {5,4,5}, {6,6,6}, {7,7,7}, {8,8,8}, {9,9,9}, {10,10,10}};
static const signed char face_dir[6][3] = {{1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}};
static const char *const block_names[NBLOCKS] = {
    "", "Grass", "Dirt", "Stone", "Wood", "Glass", "Leaves", "Bedrock", "Coal Ore", "Iron Ore"};
static const unsigned short mip_offset[5] = {0, 256, 320, 336, 340};

/* ---------------- player, inventory and session state ---------------- */
static int px = 40 * ONE + ONE / 2, py = 30 * ONE, pz = 40 * ONE + ONE / 2, vy;
static int yaw = 391, pitch = -163, on_ground;
static unsigned char inv_id[NSLOTS] = {GRASS, DIRT, STONE, WOOD, GLASS};
static unsigned char inv_cnt[NSLOTS] = {64, 64, 64, 64, 64};
static int sel, held_id, held_cnt, state = TITLE, kslot, use_mouse = 1, debug_line = 1;
static int target, hit[3], prev[3];
static int mouse_x = 320, mouse_y = 200, draw_pointer = 1;
static const unsigned char *pressed, *typed, *keys;
static const unsigned char no_keys[256];
static int scale = 1, auto_scale = 1, fps, frames, fps_ms, play_ms, slowest;
static int SW = 640, SH = 400;
static const char *notice;
static int notice_ms, worst_ms;

/* ---------------- render targets ---------------- */
static BgxCanvas screen, view;
static int RW, RH;

static int isin(int a) { return bc_sin[a & (ANGLES - 1)]; }
static int icos(int a) { return bc_sin[(a + ANGLES / 4) & (ANGLES - 1)]; }
static int fmul(int a, int b) { return (int)(((long long)a * b) >> 16); }
static int iabs(int a) { return a < 0 ? -a : a; }
static int floor_fp(int v) { return v >> 16; }
static int held(unsigned int code) { return keys[code & 255]; }

/* ============================ world ============================ */
static int hash3(int x, int y, int z)
{
    unsigned int h = (unsigned int)x * 374761393u + (unsigned int)y * 668265263u +
                     (unsigned int)z * 1103515245u + seed;
    h ^= h >> 13; h *= 1274126177u; h ^= h >> 16;
    return (int)h;
}
static int smooth(int f, int s) { return ((3 << s) - 2 * f) * f * f >> (2 * s); }
static unsigned int rng(void)
{ unsigned int x = rng_state; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return rng_state = x; }
static int rng_mod(unsigned int n) { return (int)(rng() % n); }

static int noise2(int x, int z, int s)
{
    int x0 = x >> s, z0 = z >> s, mask = (1 << s) - 1;
    int tx = smooth(x & mask, s), tz = smooth(z & mask, s);
    int a = hash3(x0, s, z0) & 255, b = hash3(x0 + 1, s, z0) & 255;
    int c = hash3(x0, s, z0 + 1) & 255, d = hash3(x0 + 1, s, z0 + 1) & 255;
    int e = a + ((b - a) * tx >> s), f = c + ((d - c) * tx >> s);
    return e + ((f - e) * tz >> s);
}
static int noise3(int x, int y, int z, int s, int salt)
{
    int x0 = x >> s, y0 = y >> s, z0 = z >> s, mask = (1 << s) - 1;
    int tx = smooth(x & mask, s), ty = smooth(y & mask, s), tz = smooth(z & mask, s), v[4];
    for (int i = 0; i < 4; i++) {
        int yy = y0 + (i & 1) + salt, zz = z0 + (i >> 1);
        int a = hash3(x0, yy, zz) & 255, b = hash3(x0 + 1, yy, zz) & 255;
        v[i] = a + ((b - a) * tx >> s);
    }
    int e = v[0] + ((v[1] - v[0]) * ty >> s), f = v[2] + ((v[3] - v[2]) * ty >> s);
    return e + ((f - e) * tz >> s);
}

static int get_block(int x, int y, int z)
{
    if ((unsigned)x >= WX || (unsigned)y >= WY || (unsigned)z >= WZ) return AIR;
    return world[(y * WZ + z) * WX + x];
}
static void set_block(int x, int y, int z, int id)
{
    if ((unsigned)x < WX && (unsigned)y < WY && (unsigned)z < WZ) world[(y * WZ + z) * WX + x] = id;
}
/* Physics: walls outside X/Z and below 0, open sky above. */
static int solid_at(int x, int y, int z)
{
    if ((unsigned)x >= WX || (unsigned)z >= WZ || y < 0) return 1;
    return y < WY && world[(y * WZ + z) * WX + x];
}
static int world_top = WY - 1;   /* highest y that holds a block */
static void column_top(int x, int z)
{
    int y = WY - 1;
    while (y >= 0 && (get_block(x, y, z) == AIR || get_block(x, y, z) == GLASS)) y--;
    ctop[z * WX + x] = y + 1;   /* glass lets the sky through */
    for (int h = WY - 1; h >= 0; h--) if (get_block(x, h, z)) { if (h > world_top) world_top = h; break; }
}

static void vein(int x, int y, int z, int ore, int length)
{
    while (length--) {
        if (get_block(x, y, z) == STONE) set_block(x, y, z, ore);
        const signed char *d = face_dir[rng_mod(6)];
        x += d[0]; y += d[1]; z += d[2];
    }
}
/* A tunnel is where two independent 3D noises are both near the middle. */
static int is_cave(int x, int y, int z, int k)
{
    return iabs(noise3(x, y, z, 3, 4096) - 128) < k && iabs(noise3(x, y, z, 3, 8192) - 128) < k;
}
static void leaves_layer(int cx, int y, int cz, int r)
{
    for (int dz = -r; dz <= r; dz++)
        for (int dx = -r; dx <= r; dx++)
            if ((iabs(dx) != r || iabs(dz) != r) && get_block(cx + dx, y, cz + dz) == AIR)
                set_block(cx + dx, y, cz + dz, LEAVES);
}
static void gen_world(void)
{
    rng_state = (seed * 0x9E3779B1u) | 1;
    for (int z = 0; z < WZ; z++)
        for (int x = 0; x < WX; x++) {
            int h = (noise2(x, z, 5) * 12 >> 8) + (noise2(x, z, 3) * 5 >> 8) + 9;
            hmap[z * WX + x] = h;
            for (int y = 0; y <= h; y++)
                set_block(x, y, z, !y ? BEDROCK : y == h ? GRASS : y >= h - 3 ? DIRT : STONE);
        }
    for (int i = 0; i < 110; i++) {   /* coal anywhere in stone */
        int x = rng_mod(WX), z = rng_mod(WZ), y = rng_mod(hmap[z * WX + x] - 4) + 2;
        vein(x, y, z, COAL, 7);
    }
    for (int i = 0; i < 60; i++) {    /* iron only in the lower layers */
        int x = rng_mod(WX), z = rng_mod(WZ), y = rng_mod(12) + 2;
        vein(x, y, z, IRON, 5);
    }
    for (int z = 0; z < WZ; z++)
        for (int x = 0; x < WX; x++) {
            int h = hmap[z * WX + x];
            for (int y = 1; y <= h; y++)   /* keep bedrock; narrow near surface */
                if (is_cave(x, y, z, y < h - 1 ? CAVE_K : CAVE_K_TOP)) set_block(x, y, z, AIR);
        }
    for (int z = 3; z < WZ - 3; z++)  /* trees, only on intact grass */
        for (int x = 3; x < WX - 3; x++) {
            int h = hmap[z * WX + x];
            if (hash3(x, 777, z) & 63 || get_block(x, h, z) != GRASS || get_block(x, h + 1, z)) continue;
            for (int y = 1; y <= 4; y++) set_block(x, h + y, z, WOOD);
            for (int y = 3; y <= 6; y++) leaves_layer(x, h + y, z, y < 5 ? 2 : 1);
        }
    world_top = 0;
    for (int z = 0; z < WZ; z++)
        for (int x = 0; x < WX; x++) column_top(x, z);
}

/* ============================ player ============================ */
static int collides(int x, int y, int z)
{
    int x0 = floor_fp(x - PLAYER_W), x1 = floor_fp(x + PLAYER_W - EPS);
    int y0 = floor_fp(y), y1 = floor_fp(y + PLAYER_H - EPS);
    int z0 = floor_fp(z - PLAYER_W), z1 = floor_fp(z + PLAYER_W - EPS);
    for (int j = y0; j <= y1; j++)
        for (int k = z0; k <= z1; k++)
            for (int i = x0; i <= x1; i++)
                if (solid_at(i, j, k)) return 1;
    return 0;
}
static void spawn(void)
{
    int x = 40, y = WY - 1;
    for (; x < WX - 1; x++) {
        for (y = WY - 1; y >= 0 && !get_block(x, y, 40); y--) {}
        if (y >= 0 && get_block(x, y, 40) == GRASS) break;
    }
    px = x * ONE + ONE / 2; py = (y + 1) * ONE; pz = 40 * ONE + ONE / 2;
}
/* dt in milliseconds, at most 10 per call so falls cannot skip a floor. */
static void physics(int dt)
{
    int forward = 0, strafe = 0;
    if (state == PLAY) {
        forward = (held(BGX_KEY_W) ? 1 : 0) - (held(BGX_KEY_S) ? 1 : 0);
        strafe = (held(BGX_KEY_D) ? 1 : 0) - (held(BGX_KEY_A) ? 1 : 0);
    }
    int s = isin(yaw), c = icos(yaw), step = 301 * dt;   /* 4.6 blocks/s */
    if (forward && strafe) step = fmul(step, 46341);
    int mx = fmul(strafe * c - forward * s, step), mz = -fmul(forward * c + strafe * s, step);
    vy -= 1704 * dt;                                       /* gravity 26 */
    if (vy < -30 * ONE) vy = -30 * ONE;
    if (state == PLAY && held(BGX_KEY_SPACE) && on_ground) vy = 550502;   /* jump 8.4 */
    if (!collides(px + mx, py, pz)) px += mx;
    if (!collides(px, py, pz + mz)) pz += mz;
    on_ground = 0;
    int ny = py + vy * dt / 1000;
    if (collides(px, ny, pz)) {
        if (vy < 0) { py = (floor_fp(ny) + 1) * ONE; on_ground = 1; }
        vy = 0;
    } else py = ny;
}

/* Voxel DDA along the view direction, reach 5 blocks. */
static void pick(void)
{
    int cp = icos(pitch);
    int d[3] = {-fmul(isin(yaw), cp), isin(pitch), -fmul(icos(yaw), cp)};
    int o[3] = {px, py + EYE, pz}, cell[3], step[3], tmax[3], tdelta[3];
    for (int i = 0; i < 3; i++) {
        cell[i] = prev[i] = floor_fp(o[i]);
        int a = iabs(d[i]);
        if (a < 8) { step[i] = 0; tdelta[i] = tmax[i] = 0x7fffffff; continue; }
        step[i] = d[i] > 0 ? 1 : -1;
        tdelta[i] = (int)(0xffffffffu / (unsigned int)a);
        int frac = o[i] & (ONE - 1);
        tmax[i] = fmul(d[i] > 0 ? ONE - frac : frac, tdelta[i]);
    }
    target = 0;
    for (int n = 0; n < 64; n++) {
        if (get_block(cell[0], cell[1], cell[2])) {
            target = 1; hit[0] = cell[0]; hit[1] = cell[1]; hit[2] = cell[2]; return;
        }
        int axis = tmax[1] < tmax[0] ? 1 : 0;
        if (tmax[2] < tmax[axis]) axis = 2;
        if (tmax[axis] > REACH) return;
        prev[0] = cell[0]; prev[1] = cell[1]; prev[2] = cell[2];
        cell[axis] += step[axis];
        tmax[axis] += tdelta[axis];
    }
}

/* ============================ sound ============================ */
/* Recorded CC0 clips (Kenney, see sounds/LICENSE.txt), stored as mu-law and
 * decoded once. Like the original, every block has a sound set: steps use it
 * quietly, placing loudly; digging has its own (pickaxe, axe, shatter).
 * The PC speaker gets short clicks. */
#include "barnocraft_sounds.h"
#define SND_RATE 22050
enum { SET_GRASS, SET_GRAVEL, SET_STONE, SET_WOOD, SET_PICK, SET_CHOP, SET_GLASS, SET_LAND, SETS };
static const unsigned char set_first[SETS] = {BCS_GRASS1, BCS_GRAVEL1, BCS_STONE1, BCS_WOOD1, BCS_PICK1, BCS_CHOP1, BCS_GLASS1, BCS_LAND};
static const unsigned char set_size[SETS] = {4, 3, 4, 4, 3, 3, 2, 1};
static short snd_pcm[BCS_TOTAL_FRAMES];
static short *snd_data[BCS_COUNT];
static int snd_kind, speaker_ms, step_ms, step_side;
static unsigned int snd_seed = 12345;

static void sound_init(void)
{
    snd_kind = bsnd_device(0, 0);
    if (snd_kind != BARNIX_SOUND_CARD || snd_data[0]) return;
    short *at = snd_pcm;
    for (int i = 0; i < BCS_COUNT; i++) { snd_data[i] = at; bsnd_ulaw(bcs_data[i], bcs_frames[i], at); at += bcs_frames[i]; }
}
/* A random clip of the set, slightly varied in volume. */
static void sound(int set, int volume, int pan)
{
    if (snd_kind == BARNIX_SOUND_CARD && snd_data[0]) {
        snd_seed = snd_seed * 1103515245 + 12345;
        int clip = set_first[set] + (int)((snd_seed >> 16) % set_size[set]);
        bsnd_play(snd_data[clip], bcs_frames[clip], SND_RATE, volume - (int)(snd_seed >> 8 & 31), pan);
    } else if (snd_kind == BARNIX_SOUND_SPEAKER && volume > 160) {
        bsnd_tone(set == SET_STONE || set == SET_PICK ? 220 : set == SET_GLASS ? 1200 : set == SET_WOOD || set == SET_CHOP ? 160 : 110);
        speaker_ms = 30;
    }
}
static int step_set(int id)
{
    return id == GRASS || id == LEAVES ? SET_GRASS : id == DIRT ? SET_GRAVEL : id == WOOD ? SET_WOOD : SET_STONE;
}
static int dig_set(int id)
{
    return id == STONE || id == BEDROCK || id == COAL || id == IRON ? SET_PICK :
           id == WOOD ? SET_CHOP : id == GLASS ? SET_GLASS : step_set(id);
}
static int ground_block(void) { return get_block(floor_fp(px), floor_fp(py - ONE / 8), floor_fp(pz)); }

/* ============================ inventory ============================ */
static void add_item(int id)
{
    for (int i = 0; i < NSLOTS; i++)
        if (inv_id[i] == id && inv_cnt[i] && inv_cnt[i] < STACK) { inv_cnt[i]++; return; }
    for (int i = 0; i < NSLOTS; i++)
        if (!inv_cnt[i]) { inv_id[i] = id; inv_cnt[i] = 1; return; }
    /* inventory full: the item is lost, as in the original */
}
static void inv_click(int slot, int right)
{
    if (slot < 0) return;
    unsigned char *id = &inv_id[slot], *cnt = &inv_cnt[slot];
    if (!right) {
        if (!held_cnt) { held_id = *id; held_cnt = *cnt; *id = *cnt = 0; return; }
        if (*cnt && *id == held_id) {           /* merge */
            int room = STACK - *cnt, n = held_cnt < room ? held_cnt : room;
            *cnt += n; held_cnt -= n; if (!held_cnt) held_id = 0;
            return;
        }
        int tid = *id, tcnt = *cnt;           /* swap, or put into an empty slot */
        *id = held_id; *cnt = held_cnt; held_id = tcnt ? tid : 0; held_cnt = tcnt;
        return;
    }
    if (!held_cnt) {                           /* take half */
        if (!*cnt) return;
        held_cnt = (*cnt + 1) / 2; held_id = *id; *cnt -= held_cnt;
        if (!*cnt) *id = 0;
        return;
    }
    if (!*cnt) *id = held_id;                  /* put one */
    else if (*id != held_id || *cnt >= STACK) return;
    (*cnt)++;
    if (!--held_cnt) held_id = 0;
}
static void close_inventory(void)
{
    while (held_cnt) { add_item(held_id); held_cnt--; }
    held_id = 0; state = PLAY;
}

static void break_block(void)
{
    pick();
    if (!target || hit[1] == 0) return;      /* bedrock layer is unbreakable */
    int id = get_block(hit[0], hit[1], hit[2]);
    if (id == BEDROCK) return;
    set_block(hit[0], hit[1], hit[2], AIR);
    sound(dig_set(id), 256, 0);
    add_item(id == GRASS ? DIRT : id);       /* grass drops dirt */
    column_top(hit[0], hit[2]);
}
static void place_block(void)
{
    if (!inv_cnt[sel]) return;
    pick();
    if (!target || (prev[0] == hit[0] && prev[1] == hit[1] && prev[2] == hit[2])) return;
    if ((unsigned)prev[0] >= WX || (unsigned)prev[1] >= WY || (unsigned)prev[2] >= WZ) return;
    if (get_block(prev[0], prev[1], prev[2])) return;
    set_block(prev[0], prev[1], prev[2], inv_id[sel]);
    if (collides(px, py, pz)) { set_block(prev[0], prev[1], prev[2], AIR); return; }
    sound(step_set(inv_id[sel]), 240, 0);
    if (!--inv_cnt[sel]) inv_id[sel] = 0;
    column_top(prev[0], prev[2]);
}

/* ============================ rendering ============================ */
#define HORIZON_R 168
#define HORIZON_G 206
#define HORIZON_B 246

/* Light of the air cell in front of a face: open sky or darker the deeper it
 * is below the highest light-blocking block of its column. */
static int face_light(int x, int y, int z)
{
    if ((unsigned)x >= WX || (unsigned)z >= WZ) return 255;
    int top = ctop[z * WX + x];
    if (y >= top) return 255;
    int light = 215 - 14 * (top - y);
    return light < 70 ? 70 : light;
}

/* Camera for one frame, shared read-only by the row bands. */
static struct {
    int f[3], u[3], dr[3], o[3], ocell[3], ofrac[3], k, lod_k;
} cam;
#define BAND_ROWS 8
static void render_band(void *unused, unsigned int band);
static void render_world(void)
{
    int s = isin(yaw), c = icos(yaw), sp = isin(pitch), cp = icos(pitch);
    /* Camera basis, 8.24 fixed point. */
    int r[3] = {c << 8, 0, -s << 8};
    cam.f[0] = -fmul(s, cp) << 8; cam.f[1] = sp << 8; cam.f[2] = -fmul(c, cp) << 8;
    cam.u[0] = fmul(s, sp) << 8; cam.u[1] = cp << 8; cam.u[2] = fmul(c, sp) << 8;
    /* tan(35 degrees)=0.7 across half the height; square pixels. */
    cam.k = 0x00b33333 / (RH / 2) /* 0.7 in 8.24 */;
    cam.o[0] = px; cam.o[1] = py + EYE; cam.o[2] = pz;
    for (int i = 0; i < 3; i++) {
        cam.ocell[i] = floor_fp(cam.o[i]); cam.ofrac[i] = cam.o[i] & 0xffff;
        cam.dr[i] = (int)(((long long)r[i] * cam.k) >> 24);
    }
    /* Texels covered by one pixel per block of depth: 16 * 1.4 / RH, 16.16. */
    cam.lod_k = (int)(22 * ONE + ONE * 2 / 5) / RH;
    /* Rows are independent: bands of 8 go to every processor core. */
    bgx_parallel(render_band, 0, (unsigned int)(RH + BAND_ROWS - 1) / BAND_ROWS);
}

static void render_band(void *unused, unsigned int band)
{
    (void)unused;
    const int *f = cam.f, *u = cam.u, *dr = cam.dr, *o = cam.o, *ocell = cam.ocell, *ofrac = cam.ofrac;
    const int k = cam.k, lod_k = cam.lod_k;
    int line[3];
    const int fog_start = 22 * ONE, fog_end = 46 * ONE, fog_scale = (256 << 16) / ((fog_end - fog_start) >> 8);
    int last_row = (int)band * BAND_ROWS + BAND_ROWS;
    if (last_row > RH) last_row = RH;
    for (int j = (int)band * BAND_ROWS; j < last_row; j++) {
        int vy2 = RH - 1 - 2 * j;   /* (H/2 - j - 0.5) * 2 */
        for (int i = 0; i < 3; i++)
            line[i] = f[i] + (int)(((long long)u[i] * k * vy2) >> 25) - dr[i] * (RW - 1) / 2;
        int dyrow = line[1] >> 10;
        int sky_k = dyrow < 0 ? 0 : dyrow > 16383 ? 256 : dyrow >> 6;
        unsigned int sky = BGX_RGB(HORIZON_R - (58 * sky_k >> 8), HORIZON_G - (44 * sky_k >> 8), HORIZON_B - (12 * sky_k >> 8));
        unsigned int *out = view.pixels + j * RW;
        int d24[3] = {line[0], line[1], line[2]};
        for (int i = 0; i < RW; i++, d24[0] += dr[0], d24[1] += dr[1], d24[2] += dr[2]) {
            int d[3] = {d24[0] >> 10, d24[1] >> 10, d24[2] >> 10}, cell[3], step[3], tm[3], td[3];
            for (int a = 0; a < 3; a++) {
                cell[a] = ocell[a];
                if (d[a] > 0) { step[a] = 1; td[a] = (1 << 30) / d[a]; tm[a] = fmul(ONE - ofrac[a], td[a]); }
                else if (d[a] < 0) { step[a] = -1; td[a] = (1 << 30) / -d[a]; tm[a] = fmul(ofrac[a], td[a]); }
                else { step[a] = 0; td[a] = tm[a] = 0x7fffffff; }
            }
            int dmax = iabs(d[0]);
            if (iabs(d[1]) > dmax) dmax = iabs(d[1]);
            if (iabs(d[2]) > dmax) dmax = iabs(d[2]);
            int idx = (cell[1] * WZ + cell[2]) * WX + cell[0], dz = step[2] * WX, dy = step[1] * WXZ;
            int acc_r = 0, acc_g = 0, acc_b = 0, trans = 256, last = AIR;
            unsigned int result = 0xffffffffu;
            for (;;) {
                int t, axis;
                if (tm[0] < tm[1] && tm[0] < tm[2]) {
                    t = tm[0]; axis = 0; cell[0] += step[0]; idx += step[0]; tm[0] += td[0];
                    if ((unsigned)cell[0] >= WX) break;
                } else if (tm[1] < tm[2]) {
                    t = tm[1]; axis = 1; cell[1] += step[1]; idx += dy; tm[1] += td[1];
                    if (cell[1] < 0 || (cell[1] > world_top && step[1] > 0)) break;   /* above every block: sky */
                } else {
                    t = tm[2]; axis = 2; cell[2] += step[2]; idx += dz; tm[2] += td[2];
                    if ((unsigned)cell[2] >= WZ) break;
                }
                if (t > fog_end) break;
                if ((unsigned)cell[1] >= WY) { last = AIR; continue; }
                int b = world[idx];
                if (!b || (b == GLASS && last == GLASS)) { last = b; continue; }
                last = b;
                /* Hit: texture coordinates from the point on the face. */
                int hx = o[0] + (int)(((long long)t * d[0]) >> 14);
                int hy = o[1] + (int)(((long long)t * d[1]) >> 14);
                int hz = o[2] + (int)(((long long)t * d[2]) >> 14);
                int tu, tv, tile, shade;
                if (axis == 1) {
                    tu = (hx >> 12) & 15; tv = (hz >> 12) & 15;
                    tile = block_tiles[b][step[1] < 0 ? 0 : 2]; shade = step[1] < 0 ? 255 : 140;
                } else {
                    tu = ((axis ? hx : hz) >> 12) & 15; tv = 15 - ((hy >> 12) & 15);
                    tile = block_tiles[b][1]; shade = axis ? 175 : 210;
                }
                /* Mip level from the pixel footprint, wider on grazing faces. */
                /* footprint * min(dmax / |d[axis]|, 3) >= 1.5 << lod, without dividing */
                int across = iabs(d[axis]) | 1, slant = dmax < 3 * across ? dmax : 3 * across;
                long long footprint = (long long)fmul(t, lod_k) * slant;
                int lod = 0;
                while (lod < 4 && footprint >= ((long long)(3 << 15) << lod) * across) lod++;
                unsigned int texel = bc_tex[tile * BC_TILE_TEXELS + mip_offset[lod] + (tv >> lod) * (16 >> lod) + (tu >> lod)];
                int light = shade * face_light(cell[0] - (axis == 0 ? step[0] : 0),
                                               cell[1] - (axis == 1 ? step[1] : 0),
                                               cell[2] - (axis == 2 ? step[2] : 0)) >> 8;
                int cr = (texel >> 16 & 255) * light >> 8, cg = (texel >> 8 & 255) * light >> 8, cb = (texel & 255) * light >> 8;
                if (target && cell[0] == hit[0] && cell[1] == hit[1] && cell[2] == hit[2]) {
                    /* Outline the targeted block: 1/32 of a block along face edges. */
                    int e0 = ((axis == 0 ? hz : hx) >> 11) & 31, e1 = ((axis == 1 ? hz : hy) >> 11) & 31;
                    if (e0 == 0 || e0 == 31 || e1 == 0 || e1 == 31) cr = cg = cb = 24;
                }
                if (t > fog_start) {
                    int fog = fmul(t - fog_start, fog_scale) >> 8;
                    if (fog > 256) fog = 256;
                    cr += (HORIZON_R - cr) * fog >> 8; cg += (HORIZON_G - cg) * fog >> 8; cb += (HORIZON_B - cb) * fog >> 8;
                }
                int alpha = texel >> 24;
                if (alpha >= 250) {
                    result = BGX_RGB((acc_r + cr * trans) >> 8, (acc_g + cg * trans) >> 8, (acc_b + cb * trans) >> 8);
                    break;
                }
                int part = trans * alpha / 255;
                acc_r += cr * part; acc_g += cg * part; acc_b += cb * part; trans -= part;
            }
            if (result == 0xffffffffu)
                result = trans == 256 ? sky : BGX_RGB((acc_r + HORIZON_R * trans) >> 8, (acc_g + HORIZON_G * trans) >> 8,
                                                      (acc_b + HORIZON_B * trans) >> 8);
            out[i] = result;
        }
    }
}

/* ---------------- HUD ---------------- */
#define PANEL 0xc6c6c6U
#define SLOT_BG 0x8b8b8bU
#define DARK 0x373737U
#define YELLOW 0xffff55U
#define GRAY 0xc0c0c0U

static char *format_int(char *out, int n)
{
    char rev[12]; int k = 0;
    if (n < 0) { *out++ = '-'; n = -n; }
    do rev[k++] = '0' + n % 10; while (n /= 10);
    while (k) *out++ = rev[--k];
    *out = 0;
    return out;
}
static char *append(char *out, const char *s) { while (*s) *out++ = *s++; *out = 0; return out; }
static void center_text(int y, const char *s, unsigned int color, int size)
{ bgx_text_shadow(&screen, (SW - bgx_text_width(s, size)) / 2, y, s, color, size); }
static void bevel(int x, int y, int w, int h, unsigned int fill, unsigned int light, unsigned int dark)
{
    bgx_fill(&screen, x, y, w, h, fill);
    bgx_fill(&screen, x, y, w, 2, light); bgx_fill(&screen, x, y, 2, h, light);
    bgx_fill(&screen, x, y + h - 2, w, 2, dark); bgx_fill(&screen, x + w - 2, y, 2, h, dark);
}
static void draw_stack(int x, int y, int id, int count)
{
    if (!count) return;
    bgx_image(&screen, x + 4, y + 4, bc_tex + block_tiles[id][1] * BC_TILE_TEXELS, 16, 16, 2);
    if (count > 1) {
        char number[12]; format_int(number, count);
        bgx_text_shadow(&screen, x + SLOT - 2 - bgx_text_width(number, 1), y + SLOT - 17, number, BGX_WHITE, 1);
    }
}
static void slot_pos(int slot, int *x, int *y)
{
    int gx = (SW - (9 * SSTEP + 20)) / 2 + 12, gy = (SH - (30 + 4 * SSTEP + 14 + 14)) / 2 + 30;
    if (slot < 9) { *x = gx + slot * SSTEP; *y = gy + 3 * SSTEP + 14; }
    else { *x = gx + (slot - 9) % 9 * SSTEP; *y = gy + (slot - 9) / 9 * SSTEP; }
}
static int slot_at(int mx, int my)
{
    for (int i = 0; i < NSLOTS; i++) {
        int x, y; slot_pos(i, &x, &y);
        if (mx >= x && mx < x + SLOT && my >= y && my < y + SLOT) return i;
    }
    return -1;
}

static void draw_hud(int fps)
{
    if (state == INVENTORY) {
        bgx_blend(&screen, 0, 0, SW, SH, BGX_BLACK, 140);
        int x0, y0; slot_pos(9, &x0, &y0);
        int w = 9 * SSTEP + 20, h = 30 + 4 * SSTEP + 14 + 14;
        bevel(x0 - 12, y0 - 30, w, h, PANEL, BGX_WHITE, 0x555555);
        bgx_text(&screen, x0, y0 - 24, "Inventory", 0x404040, 1);
        for (int i = 0; i < NSLOTS; i++) {
            int x, y; slot_pos(i, &x, &y);
            bevel(x, y, SLOT, SLOT, SLOT_BG, DARK, BGX_WHITE);
            if (!use_mouse && i == kslot) bgx_frame(&screen, x - 1, y - 1, SLOT + 2, SLOT + 2, 2, YELLOW);
            else if (use_mouse && slot_at(mouse_x, mouse_y) == i) bgx_blend(&screen, x + 2, y + 2, SLOT - 4, SLOT - 4, BGX_WHITE, 70);
            draw_stack(x, y, inv_id[i], inv_cnt[i]);
        }
        center_text(y0 - 30 + h + 8, "LMB/Space take/put/swap   RMB/X half/one   E close", BGX_WHITE, 1);
        if (held_cnt) {
            int x, y;
            if (use_mouse) { x = mouse_x - SLOT / 2; y = mouse_y - SLOT / 2; }
            else { slot_pos(kslot, &x, &y); x += 10; y -= 10; }
            draw_stack(x, y, held_id, held_cnt);
        }
        if (use_mouse && draw_pointer) bgx_cursor(&screen, mouse_x, mouse_y);
    } else {
        int bx = (SW - 9 * SSTEP + 4) / 2, by = SH - SSTEP - 4;
        bgx_blend(&screen, bx - 4, by - 4, 9 * SSTEP + 4, SSTEP + 4, BGX_BLACK, 110);
        for (int i = 0; i < 9; i++) {
            int x = bx + i * SSTEP;
            bgx_blend(&screen, x, by, SLOT, SLOT, 0x606060, 120);
            bgx_frame(&screen, x, by, SLOT, SLOT, i == sel ? 3 : 1, i == sel ? BGX_WHITE : 0x8b8b8b);
            draw_stack(x, by, inv_id[i], inv_cnt[i]);
        }
        if (inv_cnt[sel] && state == PLAY) center_text(by - 22, block_names[inv_id[sel]], BGX_WHITE, 1);
        if (state == PLAY) {
            bgx_fill(&screen, SW / 2 - 8, SH / 2 - 1, 17, 3, 0x202020);
            bgx_fill(&screen, SW / 2 - 1, SH / 2 - 8, 3, 17, 0x202020);
            bgx_fill(&screen, SW / 2 - 7, SH / 2, 15, 1, BGX_WHITE);
            bgx_fill(&screen, SW / 2, SH / 2 - 7, 1, 15, BGX_WHITE);
        } else {
            bgx_blend(&screen, 0, 0, SW, by - 8, BGX_BLACK, 150);
            if (state == TITLE) {
                center_text(SH / 2 - 116, "BARNOCRAFT", YELLOW, 4);
                center_text(SH / 2 - 40, "Click or press Enter to play", BGX_WHITE, 1);
            } else {
                center_text(SH / 2 - 104, "Paused", YELLOW, 3);
                center_text(SH / 2 - 40, "Click / Enter / Esc - resume      Q - quit", BGX_WHITE, 1);
            }
            center_text(SH / 2 + 6, "WASD move   Space jump   Mouse or arrows look", GRAY, 1);
            center_text(SH / 2 + 26, "LMB/X break   RMB/C place   1-9/wheel select", GRAY, 1);
            center_text(SH / 2 + 46, "E inventory   Esc pause   F detail   F3 debug line   F11 full screen", GRAY, 1);
        }
    }
    if (debug_line) {
        char text[96], *p = text;
        p = append(p, "FPS "); p = format_int(p, fps);
        p = append(p, " (max "); p = format_int(p, worst_ms); p = append(p, " ms)");
        p = append(p, "   XYZ "); p = format_int(p, floor_fp(px)); p = append(p, " ");
        p = format_int(p, floor_fp(py)); p = append(p, " "); p = format_int(p, floor_fp(pz));
        p = append(p, "   chunk "); p = format_int(p, floor_fp(px) >> 4); p = append(p, ",");
        p = format_int(p, floor_fp(pz) >> 4);
        p = append(p, scale == 1 ? "   detail high" : scale == 2 ? "   detail medium" : "   detail low");
        p = append(p, "   cores "); p = format_int(p, (int)bgx_cpus());
        bgx_text_shadow(&screen, 8, 6, text, BGX_WHITE, 1);
    }
    if (notice_ms > 0) center_text(30, notice, YELLOW, 1);
}

static void set_notice(const char *text) { notice = text; notice_ms = 2500; }
static void set_scale(int value)
{
    scale = value; RW = (SW + scale - 1) / scale; RH = (SH + scale - 1) / scale;
    view.width = RW; view.height = RH;
}
static const char *detail_name(void) { return scale == 1 ? "Detail: high" : scale == 2 ? "Detail: medium" : "Detail: low"; }

void bc_init(void)
{
    sound_init();
    gen_world();
    spawn();
}

int bc_frame(BgxCanvas *canvas, unsigned int *view_buffer, const BcInput *in, int dt)
{
    screen = *canvas; SW = canvas->width; SH = canvas->height;
    pressed = in->pressed; typed = in->typed; keys = in->held; draw_pointer = in->draw_pointer;
    view.pixels = view_buffer;
    set_scale(scale);
    if (dt > 1000) dt = 1000;
    frames++; fps_ms += dt;
    if (dt > slowest) slowest = dt;
    if (fps_ms >= 1000) { fps = frames * 1000 / fps_ms; worst_ms = slowest; frames = 0; fps_ms = slowest = 0; }
    if (notice_ms > 0) notice_ms -= dt;
    if (dt > 50) dt = 50;

    if (in->moved && (in->mouse_x != mouse_x || in->mouse_y != mouse_y)) use_mouse = 1;
    mouse_x = in->mouse_x; mouse_y = in->mouse_y;
    unsigned int click = in->clicked;

    if (pressed[BGX_KEY_F3]) debug_line = !debug_line;
    if (pressed[BGX_KEY_F]) { auto_scale = 0; set_scale(scale == 1 ? 2 : scale == 2 ? 4 : 1); set_notice(detail_name()); }

    if (state == TITLE || state == PAUSED) {
        if (state == PAUSED && pressed[BGX_KEY_Q]) { bsnd_tone(0); return BC_QUIT; }
        if (pressed[BGX_KEY_ENTER] || pressed[BGX_KEY_SPACE] || (click & 3) || (state == PAUSED && pressed[BGX_KEY_ESC]) ||
            (state == TITLE && (pressed[BGX_KEY_W] || pressed[BGX_KEY_E]))) {
            state = PLAY;
            click = 0;
            pressed = typed = no_keys;
        }
    } else if (state == INVENTORY) {
        if (pressed[BGX_KEY_E] || pressed[BGX_KEY_ESC]) close_inventory();
        else {
            int row = kslot < 9 ? 3 : (kslot - 9) / 9, col = kslot % 9;
            if (typed[BGX_KEY_LEFT] || typed[BGX_KEY_RIGHT] || typed[BGX_KEY_UP] || typed[BGX_KEY_DOWN]) {
                use_mouse = 0;
                if (typed[BGX_KEY_LEFT]) col = (col + 8) % 9;
                if (typed[BGX_KEY_RIGHT]) col = (col + 1) % 9;
                if (typed[BGX_KEY_UP]) row = (row + 3) % 4;
                if (typed[BGX_KEY_DOWN]) row = (row + 1) % 4;
                kslot = row == 3 ? col : 9 + row * 9 + col;
            }
            if (pressed[BGX_KEY_SPACE] || pressed[BGX_KEY_ENTER]) inv_click(kslot, 0);
            if (pressed[BGX_KEY_X]) inv_click(kslot, 1);
            int slot = slot_at(mouse_x, mouse_y);
            if (slot >= 0 && use_mouse) kslot = slot;
            if (click & 1) inv_click(slot, 0);
            if (click & 2) inv_click(slot, 1);
        }
    } else {
        if (pressed[BGX_KEY_ESC] || in->release) state = PAUSED;
        else if (pressed[BGX_KEY_E]) { state = INVENTORY; kslot = sel; }
        for (int i = 0; i < 9; i++) if (pressed[BGX_KEY_1 + i]) sel = i;
        if (in->wheel) sel = ((sel - in->wheel) % 9 + 9) % 9;
        /* Mouse look: 0.0024 rad per count, from unclamped motion. */
        yaw -= in->dx * 25 >> 4; pitch -= in->dy * 25 >> 4;
        int turn = dt * 1400 >> 10;
        if (held(BGX_KEY_LEFT)) yaw += turn;
        if (held(BGX_KEY_RIGHT)) yaw -= turn;
        if (held(BGX_KEY_UP)) pitch += turn;
        if (held(BGX_KEY_DOWN)) pitch -= turn;
        if (pitch > PITCH_MAX) pitch = PITCH_MAX;
        if (pitch < -PITCH_MAX) pitch = -PITCH_MAX;
        yaw &= ANGLES - 1;
        if ((click & 1) || pressed[BGX_KEY_X]) break_block();
        if ((click & 2) || pressed[BGX_KEY_C]) place_block();
    }
    int was_on_ground = on_ground, fall = vy;
    for (int left = dt; left > 0; left -= 10) physics(left < 10 ? left : 10);
    if (py < -16 * ONE) spawn();   /* never fall forever below the world */
    if (state == PLAY && on_ground && !was_on_ground && fall < -6 * ONE) { sound(SET_LAND, 200, 0); sound(step_set(ground_block()), 170, 0); }
    if (state == PLAY && on_ground && (held(BGX_KEY_W) || held(BGX_KEY_A) || held(BGX_KEY_S) || held(BGX_KEY_D))) {
        if ((step_ms += dt) >= 400) { step_ms = 0; step_side = !step_side; sound(step_set(ground_block()), 120, step_side ? 50 : -50); }
    } else step_ms = 340;   /* the first step sounds soon after starting to walk */
    if (speaker_ms > 0 && (speaker_ms -= dt) <= 0) bsnd_tone(0);

    /* Lower the detail if the machine cannot keep up (F sets it by hand). */
    if (state == PLAY) play_ms += dt;
    if (auto_scale && play_ms > 4000 && fps && ((scale == 1 && fps < 20) || (scale == 2 && fps < 12))) {
        set_scale(scale * 2); play_ms = 0;
        if (scale == 4) auto_scale = 0;
        set_notice("Slow machine: detail lowered (F changes it)");
    }
    pick();
    render_world();
    bgx_upscale(&screen, 0, 0, &view, scale);
    draw_hud(fps);
    return state == PLAY ? BC_CAPTURE : 0;
}
