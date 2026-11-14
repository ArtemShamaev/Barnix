; =====================================================================
;  BARNOCRAFT  -  voxel prototype, pure NASM x86-64 (Linux)
;  External libs: libX11 (window/input), libGL (rasterisation only).
;  Everything else - world gen, caves, ores, meshing, lighting, physics,
;  raycast, inventory, PNG decoder + zlib inflate - is in this file.
;
;  Assets: textures.png (4x4 atlas) is embedded with incbin at build time.
;  If a textures.png lies in the current directory at run time, it is
;  loaded instead, so textures can be repainted without rebuilding.
;
;  Build:
;    nasm -f elf64 barnocraft.asm -o barnocraft.o
;    gcc -no-pie barnocraft.o -o barnocraft -lX11 -l:libGL.so.1
; =====================================================================
default rel
bits 64

; ---------------- settings ----------------
%define WX          80
%define WZ          80
%define WY          32
%define CHUNK       16
%define NCH         5
%define NCHUNKS     25
%define WIN_W0      1024
%define WIN_H0      640
%define SEED        1337
%define CAVE_K      22              ; tunnel width (noise band)
%define CAVE_K_TOP  7               ; narrower near surface -> few entrances
%define NSLOTS      36              ; 9 hotbar + 27 inventory
%define STACK       64
%define SLOT        40              ; slot size in pixels
%define SSTEP       44              ; slot pitch
%define VSIZE       24              ; vertex: T2F C4UB V3F
%define FILEMAX     4194304
%define IDATMAX     4194304
%define RAWMAX      4456448
%define PNGMAXDIM   1024

; ---------------- block ids ----------------
%define AIR         0
%define GRASS       1
%define DIRT        2
%define STONE       3
%define WOOD        4
%define GLASS       5
%define LEAVES      6
%define BEDROCK     7
%define COAL        8
%define IRON        9

; ---------------- GL constants ----------------
%define GL_DEPTH_TEST       0x0B71
%define GL_FOG             0x0B60
%define GL_BLEND           0x0BE2
%define GL_CULL_FACE       0x0B44
%define GL_TEXTURE_2D      0x0DE1
%define GL_PROJECTION      0x1701
%define GL_MODELVIEW       0x1700
%define GL_COMPILE         0x1300
%define GL_LINES           1
%define GL_QUADS           7
%define GL_T2F_C4UB_V3F    0x2A29
%define GL_UNSIGNED_BYTE   0x1401
%define GL_RGBA            0x1908
%define GL_NEAREST         0x2600
%define GL_TEX_MAG         0x2800
%define GL_TEX_MIN         0x2801
%define GL_SRC_ALPHA       0x0302
%define GL_ONE_MINUS_SRC_ALPHA 0x0303
%define GL_FOG_MODE        0x0B65
%define GL_LINEAR          0x2601
%define GL_FOG_START       0x0B63
%define GL_FOG_END         0x0B64
%define GL_FOG_COLOR       0x0B66

; ---------------- X11 event types ----------------
%define KeyPress        2
%define KeyRelease      3
%define ButtonPress     4
%define MotionNotify    6
%define FocusOut        10
%define ConfigureNotify 22
%define ClientMessage   33

extern XOpenDisplay, XDefaultScreen, XRootWindow, XCreateColormap
extern XCreateWindow, XStoreName, XMapWindow, XInternAtom, XSetWMProtocols
extern XPending, XNextEvent, XLookupKeysym, XGrabPointer, XUngrabPointer
extern XWarpPointer, XDefineCursor, XUndefineCursor, XCreateBitmapFromData
extern XCreatePixmapCursor, XLoadQueryFont, XkbSetDetectableAutoRepeat
extern XCloseDisplay
extern glXChooseVisual, glXCreateContext, glXMakeCurrent, glXSwapBuffers
extern glXUseXFont, glXDestroyContext
extern glClearColor, glClear, glEnable, glDisable, glMatrixMode, glLoadIdentity
extern glFrustum, glOrtho, glRotatef, glTranslatef, glViewport, glCallList
extern glGenLists, glNewList, glEndList, glInterleavedArrays, glDrawArrays
extern glBlendFunc, glDepthMask, glFogi, glFogf, glFogfv, glColor4ub
extern glRecti, glBegin, glEnd, glVertex3f, glVertex2i, glTexCoord2f
extern glLineWidth, glRasterPos2i, glListBase, glCallLists
extern glGenTextures, glBindTexture, glTexParameteri, glTexImage2D
extern puts

global main

; =====================================================================
section .data
align 16
signmask4:  dd 0x80000000, 0, 0, 0
dsignmask:  dq 0x8000000000000000, 0

f_one       dd 1.0
f_two       dd 2.0
f_half      dd 0.5
f_quarter   dd 0.25
f_uvlo      dd 0.0005           ; tile inset (no bleeding between tiles)
f_uvhi      dd 0.2495
f_eye       dd 1.62
f_pw        dd 0.3
f_ph        dd 1.8
f_eps       dd 0.001
f_speed     dd 4.6
f_grav      dd 26.0
f_jump      dd 8.4
f_minvy     dd -30.0
f_reach     dd 5.0
f_big       dd 1.0e30
f_tiny      dd 1.0e-6
f_sens      dd 0.0024
f_pmax      dd 1.55
f_pmin      dd -1.55
f_rad2deg   dd 57.29578
f_dtmax     dd 0.05
f_hl_lo     dd -0.003
f_hl_hi     dd 1.003
f_fogs      dd 32.0
f_foge      dd 90.0
d_nsec      dq 1.0e-9
d_ftop      dq 0.0350103
d_near      dq 0.05
d_far       dq 250.0
d_m1        dq -1.0
d_p1        dq 1.0

sky:        dd 0.55, 0.76, 0.96, 1.0
seed        dd SEED
vis_attr:   dd 4, 5, 12, 16, 0

; ---- atlas tiles per block: top, side, bottom ----
block_tiles:
    db 0,0,0        ; air
    db 0,1,2        ; grass
    db 2,2,2        ; dirt
    db 3,3,3        ; stone
    db 5,4,5        ; wood (log)
    db 6,6,6        ; glass
    db 7,7,7        ; leaves
    db 8,8,8        ; bedrock
    db 9,9,9        ; coal ore
    db 10,10,10     ; iron ore

; ---- faces: 0 +X, 1 -X, 2 +Y, 3 -Y, 4 +Z, 5 -Z ----
face_dir:
    db  1, 0, 0,  -1, 0, 0,   0, 1, 0,   0,-1, 0,   0, 0, 1,   0, 0,-1
face_corners:
    db 1,0,0, 1,1,0, 1,1,1, 1,0,1
    db 0,0,0, 0,0,1, 0,1,1, 0,1,0
    db 0,1,0, 0,1,1, 1,1,1, 1,1,0
    db 0,0,0, 1,0,0, 1,0,1, 0,0,1
    db 0,0,1, 1,0,1, 1,1,1, 0,1,1
    db 0,0,0, 0,1,0, 1,1,0, 1,0,0
face_uv:                            ; (u,v) per corner, v=0 is the top of a tile
    db 0,1, 0,0, 1,0, 1,1
    db 0,1, 1,1, 1,0, 0,0
    db 0,0, 0,1, 1,1, 1,0
    db 0,0, 1,0, 1,1, 0,1
    db 0,1, 1,1, 1,0, 0,0
    db 0,1, 0,0, 1,0, 1,1
face_kind:  db 1, 1, 0, 2, 1, 1
align 4
face_shade: dd 210, 210, 255, 140, 175, 175

edges:      db 0,1, 2,3, 4,5, 6,7,  0,2, 1,3, 4,6, 5,7,  0,4, 1,5, 2,6, 3,7

; ---- isometric inventory icon: 3 faces x 4 points (dx,dy in units) ----
icon_pts:
    db  0,-2,  2,-1,  0, 0, -2,-1   ; top
    db -2,-1,  0, 0,  0, 2, -2, 1   ; left
    db  0, 0,  2,-1,  2, 1,  0, 2   ; right
icon_uv:    db 0,0, 1,0, 1,1, 0,1
align 4
icon_col:   dd 0xFFFFFFFF, 0xFFD2D2D2, 0xFF9C9C9C

; ---- deflate tables ----
align 4
lbase:  dd 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258
lext:   dd 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0
dbase:  dd 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577
dext:   dd 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13
clorder: db 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15

; ---- embedded texture atlas ----
png_embedded:
    incbin "textures.png"
png_embedded_len equ $ - png_embedded

s_title     db "Barnocraft (NASM x86-64)", 0
s_wmdel     db "WM_DELETE_WINDOW", 0
s_font1     db "9x15", 0
s_font2     db "fixed", 0
s_texfile   db "textures.png", 0
s_nodisp    db "barnocraft: cannot open X display", 0
s_novis     db "barnocraft: no suitable GLX visual", 0
s_notex     db "barnocraft: texture atlas could not be decoded", 0
s_logo      db "B A R N O C R A F T", 0
s_click     db "Click to play", 0
s_help      db "WASD move  SPACE jump  LMB break  RMB place  1-9/wheel select  E inventory  ESC release", 0
s_invtitle  db "Inventory", 0
s_invhelp   db "LMB take/put/swap   RMB half/one   E close", 0
s_fps       db "FPS ", 0
s_xyz       db "   XYZ ", 0
s_sp        db " ", 0
s_chunk     db "   chunk ", 0
s_comma     db ",", 0

n_air       db "", 0
n_grass     db "Grass", 0
n_dirt      db "Dirt", 0
n_stone     db "Stone", 0
n_wood      db "Wood", 0
n_glass     db "Glass", 0
n_leaves    db "Leaves", 0
n_bedrock   db "Bedrock", 0
n_coal      db "Coal Ore", 0
n_iron      db "Iron Ore", 0
align 8
block_names: dq n_air, n_grass, n_dirt, n_stone, n_wood, n_glass
             dq n_leaves, n_bedrock, n_coal, n_iron

; ---- inventory: starting kit in the hotbar ----
inv_id:     db GRASS, DIRT, STONE, WOOD, GLASS, 0, 0, 0, 0
            times NSLOTS-9 db 0
inv_cnt:    db 64, 64, 64, 64, 64, 0, 0, 0, 0
            times NSLOTS-9 db 0

; ---------------- player state ----------------
px          dd 40.5
py          dd 30.0
pz          dd 40.5
vy          dd 0.0
yaw         dd 0.6
pitch       dd -0.25
win_w       dd WIN_W0
win_h       dd WIN_H0
running     db 1

; =====================================================================
section .bss
alignb 16
world       resb WX*WY*WZ           ; index = (y*WZ+z)*WX+x
hmap        resb WX*WZ              ; terrain height (before caves)
ctop        resb WX*WZ              ; 1 + highest light-blocking block, 0 = none
alignb 16
vbuf        resb 6*1024*1024        ; opaque vertices of one chunk
gbuf        resb 6*1024*1024        ; glass vertices of one chunk
file_buf    resb FILEMAX
idat_buf    resb IDATMAX
raw_buf     resb RAWMAX
tex_rgba    resb PNGMAXDIM*PNGMAXDIM*4
xev         resb 192
swa         resb 112
black_col   resb 16
blank_bits  resb 8
ts          resq 2
sleep_ts    resq 2
sbuf        resb 256
numtmp      resb 16

dpy         resq 1
root        resq 1
win         resq 1
vinfo       resq 1
glctx       resq 1
wm_delete   resq 1
blank_cursor resq 1
t_last      resq 1
fps_acc     resq 1
screen      resd 1
list_base   resd 1
font_base   resd 1
font_w      resd 1
fps_frames  resd 1
fps_val     resd 1
sb_len      resd 1
sel         resd 1                  ; hotbar slot 0..8
tex_id      resd 1
rng_state   resd 1
mouse_x     resd 1
mouse_y     resd 1
on_ground   resb 1
locked      resb 1
inv_open    resb 1
held_id     resb 1
held_cnt    resb 1
            resb 3
keys:
key_w       resb 1
key_a       resb 1
key_s       resb 1
key_d       resb 1
key_sp      resb 1
            resb 3

alignb 4
ray_o       resd 3
ray_d       resd 3
cell        resd 3
rstep       resd 3
tmax        resd 3
tdelta      resd 3
prev        resd 3
hit         resd 3
hit_flag    resd 1
tmp_cp      resd 1

; noise3 scratch
n3_x0       resd 1
n3_tx       resd 1
n3_s        resd 1
n3_salt     resd 1

; inflate / png state
alignb 8
in_ptr      resq 1
in_end      resq 1
out_ptr     resq 1
out_start   resq 1
out_end     resq 1
bitbuf      resd 1
bitcnt      resd 1
inf_err     resd 1
inf_last    resd 1
png_w       resd 1
png_h       resd 1
png_bpp     resd 1
lencnt      resd 16
lensym      resd 320
distcnt     resd 16
distsym     resd 32
huf_offs    resd 16
cl_len      resd 320                ; code lengths for construct()

; =====================================================================
section .text

; =====================================================================
;  math helpers
; =====================================================================
sincos:                             ; xmm0 angle -> xmm0 sin, xmm1 cos
    movss   [rsp-4], xmm0
    fld     dword [rsp-4]
    fsincos
    fstp    dword [rsp-4]
    movss   xmm1, [rsp-4]
    fstp    dword [rsp-4]
    movss   xmm0, [rsp-4]
    ret

ffloor:                             ; xmm0 -> eax = floor
    roundss xmm0, xmm0, 9
    cvttss2si eax, xmm0
    ret

hash3:                              ; edi x, esi y, edx z -> eax
    imul    eax, edi, 374761393
    imul    ecx, esi, 668265263
    add     eax, ecx
    imul    ecx, edx, 1103515245
    add     eax, ecx
    add     eax, [seed]
    mov     ecx, eax
    shr     ecx, 13
    xor     eax, ecx
    imul    eax, eax, 1274126177
    mov     ecx, eax
    shr     ecx, 16
    xor     eax, ecx
    ret

smooth:                             ; edi f, esi s -> f*f*(3S-2f) >> 2s
    mov     ecx, esi
    mov     eax, 3
    shl     eax, cl
    lea     edx, [rdi+rdi]
    sub     eax, edx
    imul    eax, edi
    imul    eax, edi
    add     ecx, ecx
    sar     eax, cl
    ret

; rng: xorshift32 -> eax ;  rng_mod: edi n -> eax = rng % n
rng:
    mov     eax, [rng_state]
    mov     ecx, eax
    shl     ecx, 13
    xor     eax, ecx
    mov     ecx, eax
    shr     ecx, 17
    xor     eax, ecx
    mov     ecx, eax
    shl     ecx, 5
    xor     eax, ecx
    mov     [rng_state], eax
    ret
rng_mod:
    call    rng
    xor     edx, edx
    div     edi
    mov     eax, edx
    ret

; noise2: edi x, esi z, edx s -> eax 0..255  (2D value noise)
noise2:
    push    rbp
    mov     rbp, rsp
    push    rbx
    push    r12
    push    r13
    push    r14
    push    r15
    sub     rsp, 40
    mov     r15d, edx
    mov     ecx, edx
    mov     eax, edi
    sar     eax, cl
    mov     r12d, eax
    mov     eax, esi
    sar     eax, cl
    mov     r13d, eax
    mov     eax, 1
    shl     eax, cl
    dec     eax
    and     edi, eax
    and     esi, eax
    mov     [rsp+28], esi
    mov     esi, r15d
    call    smooth
    mov     [rsp], eax
    mov     edi, [rsp+28]
    mov     esi, r15d
    call    smooth
    mov     [rsp+4], eax
    mov     edi, r12d
    mov     esi, r15d
    mov     edx, r13d
    call    hash3
    and     eax, 255
    mov     [rsp+8], eax
    lea     edi, [r12+1]
    mov     esi, r15d
    mov     edx, r13d
    call    hash3
    and     eax, 255
    mov     [rsp+12], eax
    mov     edi, r12d
    mov     esi, r15d
    lea     edx, [r13+1]
    call    hash3
    and     eax, 255
    mov     [rsp+16], eax
    lea     edi, [r12+1]
    mov     esi, r15d
    lea     edx, [r13+1]
    call    hash3
    and     eax, 255
    mov     [rsp+20], eax
    mov     ecx, r15d
    mov     eax, [rsp+12]
    sub     eax, [rsp+8]
    imul    eax, [rsp]
    sar     eax, cl
    add     eax, [rsp+8]
    mov     ebx, eax
    mov     eax, [rsp+20]
    sub     eax, [rsp+16]
    imul    eax, [rsp]
    sar     eax, cl
    add     eax, [rsp+16]
    sub     eax, ebx
    imul    eax, [rsp+4]
    sar     eax, cl
    add     eax, ebx
    add     rsp, 40
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
    pop     rbp
    ret

; n3_lx: esi yy, edx zz -> eax = lerp along x of the two lattice values
n3_lx:
    push    rbx
    push    r12
    push    r13
    mov     r12d, esi
    add     r12d, [n3_salt]
    mov     r13d, edx
    mov     edi, [n3_x0]
    mov     esi, r12d
    mov     edx, r13d
    call    hash3
    and     eax, 255
    mov     ebx, eax
    mov     edi, [n3_x0]
    inc     edi
    mov     esi, r12d
    mov     edx, r13d
    call    hash3
    and     eax, 255
    sub     eax, ebx
    imul    eax, [n3_tx]
    mov     ecx, [n3_s]
    sar     eax, cl
    add     eax, ebx
    pop     r13
    pop     r12
    pop     rbx
    ret

; noise3: edi x, esi y, edx z, ecx s, r8d salt -> eax 0..255 (3D value noise)
noise3:
    push    rbp
    mov     rbp, rsp
    push    rbx
    push    r12
    push    r13
    push    r14
    push    r15
    sub     rsp, 24
    mov     [n3_s], ecx
    mov     [n3_salt], r8d
    mov     eax, edi
    sar     eax, cl
    mov     [n3_x0], eax
    mov     eax, esi
    sar     eax, cl
    mov     r13d, eax               ; y0
    mov     eax, edx
    sar     eax, cl
    mov     r14d, eax               ; z0
    mov     r15d, 1
    shl     r15d, cl
    dec     r15d                    ; mask
    mov     ebx, esi
    and     ebx, r15d               ; fy
    mov     r12d, edx
    and     r12d, r15d              ; fz
    and     edi, r15d               ; fx
    mov     esi, [n3_s]
    call    smooth
    mov     [n3_tx], eax
    mov     edi, ebx
    mov     esi, [n3_s]
    call    smooth
    mov     ebx, eax                ; ty
    mov     edi, r12d
    mov     esi, [n3_s]
    call    smooth
    mov     r12d, eax               ; tz
    mov     esi, r13d
    mov     edx, r14d
    call    n3_lx
    mov     [rsp], eax              ; a (y0,z0)
    lea     esi, [r13+1]
    mov     edx, r14d
    call    n3_lx
    mov     [rsp+4], eax            ; b (y1,z0)
    mov     esi, r13d
    lea     edx, [r14+1]
    call    n3_lx
    mov     [rsp+8], eax            ; c (y0,z1)
    lea     esi, [r13+1]
    lea     edx, [r14+1]
    call    n3_lx
    mov     [rsp+12], eax           ; d (y1,z1)
    mov     ecx, [n3_s]
    mov     eax, [rsp+4]
    sub     eax, [rsp]
    imul    eax, ebx
    sar     eax, cl
    add     eax, [rsp]
    mov     r13d, eax               ; e
    mov     eax, [rsp+12]
    sub     eax, [rsp+8]
    imul    eax, ebx
    sar     eax, cl
    add     eax, [rsp+8]            ; f
    sub     eax, r13d
    imul    eax, r12d
    sar     eax, cl
    add     eax, r13d
    add     rsp, 24
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
    pop     rbp
    ret

; =====================================================================
;  world access
; =====================================================================
get_block:                          ; edi x, esi y, edx z -> eax (outside = air)
    cmp     edi, WX
    jae     .out
    cmp     esi, WY
    jae     .out
    cmp     edx, WZ
    jae     .out
    imul    eax, esi, WX*WZ
    imul    ecx, edx, WX
    add     eax, ecx
    add     eax, edi
    lea     rcx, [world]
    movzx   eax, byte [rcx+rax]
    ret
.out:
    xor     eax, eax
    ret

set_block:                          ; edi x, esi y, edx z, ecx id
    cmp     edi, WX
    jae     .out
    cmp     esi, WY
    jae     .out
    cmp     edx, WZ
    jae     .out
    imul    eax, esi, WX*WZ
    imul    r8d, edx, WX
    add     eax, r8d
    add     eax, edi
    lea     r8, [world]
    mov     [r8+rax], cl
.out:
    ret

solid_at:                           ; physics: walls outside X/Z and below 0
    cmp     edi, WX
    jae     .yes
    cmp     edx, WZ
    jae     .yes
    test    esi, esi
    js      .yes
    cmp     esi, WY
    jge     .no
    jmp     get_block
.yes:
    mov     eax, 1
    ret
.no:
    xor     eax, eax
    ret

; column_top: edi x, esi z -> recompute ctop for one column
column_top:
    push    rbx
    push    r12
    push    r13
    mov     r12d, edi
    mov     r13d, esi
    mov     ebx, WY-1
.l:
    mov     edi, r12d
    mov     esi, ebx
    mov     edx, r13d
    call    get_block
    test    eax, eax
    jz      .n
    cmp     eax, GLASS              ; glass lets the sky through
    jne     .f
.n:
    dec     ebx
    jns     .l
.f:
    inc     ebx                     ; 0 if nothing blocks the sky
    imul    eax, r13d, WX
    add     eax, r12d
    lea     rcx, [ctop]
    mov     [rcx+rax], bl
    pop     r13
    pop     r12
    pop     rbx
    ret

all_tops:
    push    rbx
    push    r12
    sub     rsp, 8
    xor     r12d, r12d
.z:
    xor     ebx, ebx
.x:
    mov     edi, ebx
    mov     esi, r12d
    call    column_top
    inc     ebx
    cmp     ebx, WX
    jb      .x
    inc     r12d
    cmp     r12d, WZ
    jb      .z
    add     rsp, 8
    pop     r12
    pop     rbx
    ret

; =====================================================================
;  world generation
; =====================================================================
terrain_height:                     ; edi x, esi z -> eax
    push    rbp
    mov     rbp, rsp
    push    rbx
    push    r12
    push    r13
    sub     rsp, 8
    mov     r12d, edi
    mov     r13d, esi
    mov     edx, 5
    call    noise2
    imul    eax, eax, 12
    sar     eax, 8
    mov     ebx, eax
    mov     edi, r12d
    mov     esi, r13d
    mov     edx, 3
    call    noise2
    imul    eax, eax, 5
    sar     eax, 8
    add     ebx, eax
    lea     eax, [rbx+9]
    add     rsp, 8
    pop     r13
    pop     r12
    pop     rbx
    pop     rbp
    ret

set_if_air:                         ; edi x, esi y, edx z, ecx id
    push    rbp
    mov     rbp, rsp
    push    rbx
    push    r12
    push    r13
    push    r14
    mov     ebx, edi
    mov     r12d, esi
    mov     r13d, edx
    mov     r14d, ecx
    call    get_block
    test    eax, eax
    jnz     .out
    mov     edi, ebx
    mov     esi, r12d
    mov     edx, r13d
    mov     ecx, r14d
    call    set_block
.out:
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
    pop     rbp
    ret

leaves_layer:                       ; edi cx, esi y, edx cz, ecx r
    push    rbp
    mov     rbp, rsp
    push    rbx
    push    r12
    push    r13
    push    r14
    push    r15
    sub     rsp, 8
    mov     r12d, edi
    mov     r13d, esi
    mov     r14d, edx
    mov     r15d, ecx
    mov     ebx, ecx
    neg     ebx
.lz:
    mov     eax, r15d
    neg     eax
    mov     [rsp], eax
.lx:
    mov     eax, [rsp]
    cdq
    xor     eax, edx
    sub     eax, edx
    cmp     eax, r15d
    jne     .put
    mov     eax, ebx
    cdq
    xor     eax, edx
    sub     eax, edx
    cmp     eax, r15d
    je      .skip
.put:
    mov     edi, r12d
    add     edi, [rsp]
    mov     esi, r13d
    mov     edx, r14d
    add     edx, ebx
    mov     ecx, LEAVES
    call    set_if_air
.skip:
    inc     dword [rsp]
    cmp     [rsp], r15d
    jle     .lx
    inc     ebx
    cmp     ebx, r15d
    jle     .lz
    add     rsp, 8
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
    pop     rbp
    ret

; vein: edi x, esi y, edx z, ecx ore id, r8d length - random walk through stone
vein:
    push    rbx
    push    r12
    push    r13
    push    r14
    push    r15
    mov     r12d, edi
    mov     r13d, esi
    mov     r14d, edx
    mov     r15d, ecx
    mov     ebx, r8d
.step:
    mov     edi, r12d
    mov     esi, r13d
    mov     edx, r14d
    call    get_block
    cmp     eax, STONE
    jne     .move
    mov     edi, r12d
    mov     esi, r13d
    mov     edx, r14d
    mov     ecx, r15d
    call    set_block
.move:
    mov     edi, 6
    call    rng_mod
    lea     rax, [rax+rax*2]
    lea     rcx, [face_dir]
    movsx   edx, byte [rcx+rax]
    add     r12d, edx
    movsx   edx, byte [rcx+rax+1]
    add     r13d, edx
    movsx   edx, byte [rcx+rax+2]
    add     r14d, edx
    dec     ebx
    jnz     .step
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
    ret

; gen_ores: coal anywhere in stone, iron only in the lower layers
gen_ores:
    push    rbx
    push    r12
    push    r13
    push    r14
    push    r15
    mov     ebx, 110                ; coal veins
.coal:
    mov     edi, WX
    call    rng_mod
    mov     r12d, eax
    mov     edi, WZ
    call    rng_mod
    mov     r13d, eax
    imul    ecx, r13d, WX
    add     ecx, r12d
    lea     rdx, [hmap]
    movzx   edi, byte [rdx+rcx]
    sub     edi, 4                  ; stone part of the column
    call    rng_mod
    lea     esi, [rax+2]
    mov     edi, r12d
    mov     edx, r13d
    mov     ecx, COAL
    mov     r8d, 7
    call    vein
    dec     ebx
    jnz     .coal
    mov     ebx, 60                 ; iron veins
.iron:
    mov     edi, WX
    call    rng_mod
    mov     r12d, eax
    mov     edi, WZ
    call    rng_mod
    mov     r13d, eax
    mov     edi, 12
    call    rng_mod
    lea     esi, [rax+2]
    mov     edi, r12d
    mov     edx, r13d
    mov     ecx, IRON
    mov     r8d, 5
    call    vein
    dec     ebx
    jnz     .iron
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
    ret

; is_cave: edi x, esi y, edx z, ecx k -> eax 1 if inside a tunnel.
; A tunnel is where two independent 3D noises are both near the middle.
is_cave:
    push    rbx
    push    r12
    push    r13
    push    r14
    push    r15
    mov     r12d, edi
    mov     r13d, esi
    mov     r14d, edx
    mov     r15d, ecx
    mov     ecx, 3
    mov     r8d, 4096
    call    noise3
    sub     eax, 128
    cdq
    xor     eax, edx
    sub     eax, edx
    cmp     eax, r15d
    jge     .no
    mov     edi, r12d
    mov     esi, r13d
    mov     edx, r14d
    mov     ecx, 3
    mov     r8d, 8192
    call    noise3
    sub     eax, 128
    cdq
    xor     eax, edx
    sub     eax, edx
    cmp     eax, r15d
    jge     .no
    mov     eax, 1
    jmp     .out
.no:
    xor     eax, eax
.out:
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
    ret

gen_caves:
    push    rbx
    push    r12
    push    r13
    push    r14
    push    r15
    xor     r12d, r12d              ; z
.z:
    xor     r13d, r13d              ; x
.x:
    imul    ecx, r12d, WX
    add     ecx, r13d
    lea     rdx, [hmap]
    movzx   r14d, byte [rdx+rcx]    ; h
    mov     ebx, 1                  ; y (keep bedrock)
.y:
    mov     ecx, CAVE_K
    lea     eax, [r14-1]
    cmp     ebx, eax
    jl      .k
    mov     ecx, CAVE_K_TOP
.k:
    mov     edi, r13d
    mov     esi, ebx
    mov     edx, r12d
    call    is_cave
    test    eax, eax
    jz      .ny
    mov     edi, r13d
    mov     esi, ebx
    mov     edx, r12d
    xor     ecx, ecx
    call    set_block
.ny:
    inc     ebx
    cmp     ebx, r14d
    jle     .y
    inc     r13d
    cmp     r13d, WX
    jb      .x
    inc     r12d
    cmp     r12d, WZ
    jb      .z
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
    ret

gen_world:
    push    rbp
    mov     rbp, rsp
    push    rbx
    push    r12
    push    r13
    push    r14
    push    r15
    sub     rsp, 8
    mov     eax, [seed]
    imul    eax, eax, 0x9E3779B1
    or      eax, 1
    mov     [rng_state], eax
    xor     r12d, r12d
.gz:
    xor     r13d, r13d
.gx:
    mov     edi, r13d
    mov     esi, r12d
    call    terrain_height
    mov     r14d, eax
    imul    ecx, r12d, WX
    add     ecx, r13d
    lea     rdx, [hmap]
    mov     [rdx+rcx], al
    xor     ebx, ebx
.gy:
    mov     ecx, BEDROCK
    test    ebx, ebx
    jz      .put
    mov     ecx, GRASS
    cmp     ebx, r14d
    je      .put
    mov     ecx, DIRT
    lea     eax, [r14-3]
    cmp     ebx, eax
    jge     .put
    mov     ecx, STONE
.put:
    mov     edi, r13d
    mov     esi, ebx
    mov     edx, r12d
    call    set_block
    inc     ebx
    cmp     ebx, r14d
    jle     .gy
    inc     r13d
    cmp     r13d, WX
    jb      .gx
    inc     r12d
    cmp     r12d, WZ
    jb      .gz

    call    gen_ores
    call    gen_caves

    ; ---- trees (only on intact grass) ----
    mov     r12d, 3
.tz:
    mov     r13d, 3
.tx:
    mov     edi, r13d
    mov     esi, 777
    mov     edx, r12d
    call    hash3
    and     eax, 63
    jnz     .tnext
    imul    ecx, r12d, WX
    add     ecx, r13d
    lea     rdx, [hmap]
    movzx   r14d, byte [rdx+rcx]
    mov     edi, r13d
    mov     esi, r14d
    mov     edx, r12d
    call    get_block
    cmp     eax, GRASS
    jne     .tnext
    mov     edi, r13d
    lea     esi, [r14+1]
    mov     edx, r12d
    call    get_block
    test    eax, eax
    jnz     .tnext
    mov     ebx, 1
.trunk:
    mov     edi, r13d
    lea     esi, [r14+rbx]
    mov     edx, r12d
    mov     ecx, WOOD
    call    set_block
    inc     ebx
    cmp     ebx, 4
    jle     .trunk
    mov     r15d, 3
.leaf:
    mov     edi, r13d
    lea     esi, [r14+r15]
    mov     edx, r12d
    mov     ecx, 2
    cmp     r15d, 5
    jb      .lr
    mov     ecx, 1
.lr:
    call    leaves_layer
    inc     r15d
    cmp     r15d, 6
    jle     .leaf
.tnext:
    inc     r13d
    cmp     r13d, WX-3
    jb      .tx
    inc     r12d
    cmp     r12d, WZ-3
    jb      .tz

    call    all_tops
    add     rsp, 8
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
    pop     rbp
    ret

; =====================================================================
;  chunk meshing
; =====================================================================
; face_light: esi face, ecx x, r8d y, r9d z -> eax 70..255
; The air cell in front of the face is lit if nothing above it blocks the sky;
; otherwise it gets darker the deeper it is under the blocking block.
face_light:
    lea     eax, [rsi+rsi*2]
    lea     r10, [face_dir]
    movsx   edx, byte [r10+rax]
    add     edx, ecx                ; nx
    movsx   r11d, byte [r10+rax+1]
    add     r11d, r8d               ; ny
    movsx   eax, byte [r10+rax+2]
    add     eax, r9d                ; nz
    cmp     edx, WX
    jae     .lit
    cmp     eax, WZ
    jae     .lit
    imul    eax, eax, WX
    add     eax, edx
    lea     r10, [ctop]
    movzx   eax, byte [r10+rax]     ; top+1
    cmp     r11d, eax
    jge     .lit
    sub     eax, r11d               ; depth below the roof
    imul    eax, eax, 14
    neg     eax
    add     eax, 215
    cmp     eax, 70
    jge     .out
    mov     eax, 70
.out:
    ret
.lit:
    mov     eax, 255
    ret

; emit_face: rdi dst, esi face, edx block, ecx x, r8d y, r9d z -> rax new dst
emit_face:
    push    rbp
    mov     rbp, rsp
    push    rbx
    push    r12
    push    r13
    push    r14
    push    r15
    sub     rsp, 24
    mov     rbx, rdi
    mov     r12d, esi
    mov     r13d, ecx
    mov     r14d, r8d
    mov     r15d, r9d
    mov     [rsp], edx
    call    face_light              ; uses esi/ecx/r8d/r9d as passed
    lea     rcx, [face_shade]
    imul    eax, [rcx+r12*4]
    shr     eax, 8
    mov     ah, al                  ; grey -> RGB, alpha 255
    mov     edx, eax
    shl     edx, 8
    mov     dl, al
    or      edx, 0xFF000000
    mov     [rsp+4], edx            ; packed RGBA
    ; atlas tile -> (tu, tv)
    mov     eax, [rsp]
    lea     eax, [rax+rax*2]
    lea     rcx, [face_kind]
    movzx   edx, byte [rcx+r12]
    add     eax, edx
    lea     rcx, [block_tiles]
    movzx   eax, byte [rcx+rax]
    mov     edx, eax
    and     edx, 3
    cvtsi2ss xmm6, edx
    mulss   xmm6, [f_quarter]       ; tu
    shr     eax, 2
    cvtsi2ss xmm7, eax
    mulss   xmm7, [f_quarter]       ; tv
    lea     rsi, [face_corners]
    imul    eax, r12d, 12
    add     rsi, rax
    lea     rdi, [face_uv]
    lea     rdi, [rdi+r12*8]
    mov     ecx, 4
.vert:
    movss   xmm0, [f_uvlo]
    cmp     byte [rdi], 0
    je      .u
    movss   xmm0, [f_uvhi]
.u:
    addss   xmm0, xmm6
    movss   [rbx], xmm0
    movss   xmm0, [f_uvlo]
    cmp     byte [rdi+1], 0
    je      .v
    movss   xmm0, [f_uvhi]
.v:
    addss   xmm0, xmm7
    movss   [rbx+4], xmm0
    mov     eax, [rsp+4]
    mov     [rbx+8], eax
    movzx   eax, byte [rsi]
    add     eax, r13d
    cvtsi2ss xmm0, eax
    movss   [rbx+12], xmm0
    movzx   eax, byte [rsi+1]
    add     eax, r14d
    cvtsi2ss xmm0, eax
    movss   [rbx+16], xmm0
    movzx   eax, byte [rsi+2]
    add     eax, r15d
    cvtsi2ss xmm0, eax
    movss   [rbx+20], xmm0
    add     rbx, VSIZE
    add     rsi, 3
    add     rdi, 2
    dec     ecx
    jnz     .vert
    mov     rax, rbx
    add     rsp, 24
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
    pop     rbp
    ret

; upload_list: edi list id, rsi buffer start, rdx buffer end
upload_list:
    push    rbp
    mov     rbp, rsp
    push    rbx
    push    r12
    mov     rbx, rsi
    mov     rax, rdx
    sub     rax, rsi
    xor     edx, edx
    mov     ecx, VSIZE
    div     rcx
    mov     r12d, eax               ; vertex count
    mov     esi, GL_COMPILE
    call    glNewList
    test    r12d, r12d
    jz      .end
    mov     edi, GL_T2F_C4UB_V3F
    xor     esi, esi
    mov     rdx, rbx
    call    glInterleavedArrays
    mov     edi, GL_QUADS
    xor     esi, esi
    mov     edx, r12d
    call    glDrawArrays
.end:
    call    glEndList
    pop     r12
    pop     rbx
    pop     rbp
    ret

; build_chunk: edi chunk index -> display lists list_base+2i (opaque), +1 (glass)
build_chunk:
    push    rbp
    mov     rbp, rsp
    push    rbx
    push    r12
    push    r13
    push    r14
    push    r15
    sub     rsp, 56
    mov     [rsp], edi
    mov     eax, edi
    xor     edx, edx
    mov     ecx, NCH
    div     ecx
    shl     edx, 4
    shl     eax, 4
    mov     [rsp+4], edx
    mov     [rsp+8], eax
    add     edx, CHUNK
    add     eax, CHUNK
    mov     [rsp+24], edx
    mov     [rsp+28], eax
    lea     r14, [vbuf]
    lea     r15, [gbuf]
    xor     ebx, ebx
.ly:
    mov     r12d, [rsp+8]
.lz:
    mov     r13d, [rsp+4]
.lx:
    mov     edi, r13d
    mov     esi, ebx
    mov     edx, r12d
    call    get_block
    test    eax, eax
    jz      .next
    mov     [rsp+12], eax
    mov     dword [rsp+16], 0
.lf:
    mov     eax, [rsp+16]
    cmp     eax, 3
    jne     .chk
    test    ebx, ebx
    jz      .fnext
.chk:
    lea     rcx, [face_dir]
    lea     rax, [rax+rax*2]
    movsx   edi, byte [rcx+rax]
    add     edi, r13d
    movsx   esi, byte [rcx+rax+1]
    add     esi, ebx
    movsx   edx, byte [rcx+rax+2]
    add     edx, r12d
    call    get_block
    mov     ecx, [rsp+12]
    cmp     ecx, GLASS
    jne     .opq
    test    eax, eax
    jnz     .fnext
    mov     rdi, r15
    mov     esi, [rsp+16]
    mov     edx, ecx
    mov     ecx, r13d
    mov     r8d, ebx
    mov     r9d, r12d
    call    emit_face
    mov     r15, rax
    jmp     .fnext
.opq:
    test    eax, eax
    jz      .vis
    cmp     eax, GLASS
    jne     .fnext
.vis:
    mov     rdi, r14
    mov     esi, [rsp+16]
    mov     edx, ecx
    mov     ecx, r13d
    mov     r8d, ebx
    mov     r9d, r12d
    call    emit_face
    mov     r14, rax
.fnext:
    inc     dword [rsp+16]
    cmp     dword [rsp+16], 6
    jb      .lf
.next:
    inc     r13d
    cmp     r13d, [rsp+24]
    jb      .lx
    inc     r12d
    cmp     r12d, [rsp+28]
    jb      .lz
    inc     ebx
    cmp     ebx, WY
    jb      .ly

    mov     edi, [rsp]
    shl     edi, 1
    add     edi, [list_base]
    lea     rsi, [vbuf]
    mov     rdx, r14
    call    upload_list
    mov     edi, [rsp]
    shl     edi, 1
    add     edi, [list_base]
    inc     edi
    lea     rsi, [gbuf]
    mov     rdx, r15
    call    upload_list

    add     rsp, 56
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
    pop     rbp
    ret

; rebuild_around: edi x, esi z -> refresh light of the column, rebuild chunk
; (and the neighbouring chunk when the block sits on a chunk border)
rebuild_around:
    push    rbp
    mov     rbp, rsp
    push    rbx
    push    r12
    push    r13
    push    r14
    mov     r12d, edi
    mov     r13d, esi
    call    column_top
    mov     ebx, r12d
    shr     ebx, 4
    mov     r14d, r13d
    shr     r14d, 4
    imul    edi, r14d, NCH
    add     edi, ebx
    call    build_chunk
    mov     eax, r12d
    and     eax, 15
    jnz     .x2
    test    ebx, ebx
    jz      .z1
    imul    edi, r14d, NCH
    lea     edi, [rdi+rbx-1]
    call    build_chunk
    jmp     .z1
.x2:
    cmp     eax, 15
    jne     .z1
    cmp     ebx, NCH-1
    jae     .z1
    imul    edi, r14d, NCH
    lea     edi, [rdi+rbx+1]
    call    build_chunk
.z1:
    mov     eax, r13d
    and     eax, 15
    jnz     .z2
    test    r14d, r14d
    jz      .out
    lea     edi, [r14-1]
    imul    edi, edi, NCH
    add     edi, ebx
    call    build_chunk
    jmp     .out
.z2:
    cmp     eax, 15
    jne     .out
    cmp     r14d, NCH-1
    jae     .out
    lea     edi, [r14+1]
    imul    edi, edi, NCH
    add     edi, ebx
    call    build_chunk
.out:
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
    pop     rbp
    ret

; =====================================================================
;  zlib inflate (RFC 1951)  -  stored, fixed and dynamic Huffman blocks
;  in_ptr/in_end: compressed stream, out_ptr/out_start/out_end: output
; =====================================================================
getbits:                            ; edi n (0..16) -> eax
    mov     eax, [bitbuf]
    mov     edx, [bitcnt]
.fill:
    cmp     edx, edi
    jae     .have
    mov     r8, [in_ptr]
    xor     r9d, r9d
    cmp     r8, [in_end]
    jae     .eof
    movzx   r9d, byte [r8]
    inc     r8
    mov     [in_ptr], r8
    jmp     .add
.eof:
    mov     dword [inf_err], 1
.add:
    mov     ecx, edx
    shl     r9d, cl
    or      eax, r9d
    add     edx, 8
    jmp     .fill
.have:
    mov     ecx, edi
    mov     r9d, 1
    shl     r9d, cl
    dec     r9d
    mov     r8d, eax
    shr     eax, cl
    sub     edx, edi
    mov     [bitbuf], eax
    mov     [bitcnt], edx
    mov     eax, r8d
    and     eax, r9d
    ret

; construct: rdi counts[16], rsi symbols[], rdx lengths (dwords), ecx n
; builds a canonical Huffman table
construct:
    push    rbx
    xor     eax, eax
.zc:
    mov     dword [rdi+rax*4], 0
    inc     eax
    cmp     eax, 16
    jb      .zc
    xor     eax, eax
.cnt:
    cmp     eax, ecx
    jae     .offs
    mov     ebx, [rdx+rax*4]
    inc     dword [rdi+rbx*4]
    inc     eax
    jmp     .cnt
.offs:
    lea     r8, [huf_offs]
    mov     dword [r8+4], 0
    mov     eax, 1
.of:
    mov     ebx, [r8+rax*4]
    add     ebx, [rdi+rax*4]
    mov     [r8+rax*4+4], ebx
    inc     eax
    cmp     eax, 15
    jb      .of
    xor     eax, eax
.sym:
    cmp     eax, ecx
    jae     .done
    mov     ebx, [rdx+rax*4]
    test    ebx, ebx
    jz      .ns
    mov     r9d, [r8+rbx*4]
    mov     [rsi+r9*4], eax
    inc     dword [r8+rbx*4]
.ns:
    inc     eax
    jmp     .sym
.done:
    pop     rbx
    ret

; decode: rdi counts, rsi symbols -> eax symbol (-1 on error)
decode:
    push    rbx
    push    r12
    push    r13
    push    r14
    push    r15
    mov     r12, rdi
    mov     r13, rsi
    xor     ebx, ebx                ; code
    xor     r14d, r14d              ; first
    xor     r15d, r15d              ; index
    mov     r10d, 1                 ; len
.l:
    mov     edi, 1
    call    getbits
    or      ebx, eax
    mov     r11d, [r12+r10*4]       ; count
    mov     eax, ebx
    sub     eax, r14d
    cmp     eax, r11d
    jl      .found
    add     r15d, r11d
    add     r14d, r11d
    shl     r14d, 1
    shl     ebx, 1
    inc     r10d
    cmp     r10d, 15
    jbe     .l
    mov     eax, -1
    jmp     .out
.found:
    add     eax, r15d
    mov     eax, [r13+rax*4]
.out:
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
    ret

; codes: decode literal/length + distance pairs until end-of-block. eax 0 ok
codes:
    push    rbx
    push    r12
    push    r13
.next:
    cmp     dword [inf_err], 0
    jne     .err
    lea     rdi, [lencnt]
    lea     rsi, [lensym]
    call    decode
    test    eax, eax
    js      .err
    cmp     eax, 256
    jb      .lit
    je      .ok
    sub     eax, 257
    cmp     eax, 29
    jae     .err
    mov     ebx, eax
    lea     rcx, [lext]
    mov     edi, [rcx+rbx*4]
    call    getbits
    lea     rcx, [lbase]
    add     eax, [rcx+rbx*4]
    mov     r12d, eax               ; length
    lea     rdi, [distcnt]
    lea     rsi, [distsym]
    call    decode
    test    eax, eax
    js      .err
    cmp     eax, 30
    jae     .err
    mov     ebx, eax
    lea     rcx, [dext]
    mov     edi, [rcx+rbx*4]
    call    getbits
    lea     rcx, [dbase]
    add     eax, [rcx+rbx*4]        ; distance
    mov     rdi, [out_ptr]
    mov     rsi, rdi
    sub     rsi, [out_start]
    cmp     rax, rsi
    ja      .err
    mov     rsi, rdi
    sub     rsi, rax
    mov     rdx, rdi
    add     rdx, r12
    cmp     rdx, [out_end]
    ja      .err
    mov     ecx, r12d
.copy:                              ; byte-wise: source may overlap
    mov     al, [rsi]
    mov     [rdi], al
    inc     rsi
    inc     rdi
    dec     ecx
    jnz     .copy
    mov     [out_ptr], rdi
    jmp     .next
.lit:
    mov     rdi, [out_ptr]
    cmp     rdi, [out_end]
    jae     .err
    mov     [rdi], al
    inc     rdi
    mov     [out_ptr], rdi
    jmp     .next
.ok:
    xor     eax, eax
    jmp     .out
.err:
    mov     eax, 1
.out:
    pop     r13
    pop     r12
    pop     rbx
    ret

inflate:                            ; -> eax 0 ok
    push    rbx
    push    r12
    push    r13
    push    r14
    push    r15
    mov     dword [bitbuf], 0
    mov     dword [bitcnt], 0
    mov     dword [inf_err], 0
.block:
    mov     edi, 1
    call    getbits
    mov     [inf_last], eax         ; last block flag
    mov     edi, 2
    call    getbits
    cmp     eax, 0
    je      .stored
    cmp     eax, 1
    je      .fixed
    cmp     eax, 2
    je      .dynamic
    jmp     .err

.stored:
    mov     dword [bitbuf], 0       ; drop to byte boundary
    mov     dword [bitcnt], 0
    mov     rsi, [in_ptr]
    lea     rax, [rsi+4]
    cmp     rax, [in_end]
    ja      .err
    movzx   ecx, word [rsi]
    movzx   eax, word [rsi+2]
    xor     eax, 0xFFFF
    cmp     eax, ecx
    jne     .err
    add     rsi, 4
    lea     rax, [rsi+rcx]
    cmp     rax, [in_end]
    ja      .err
    mov     rdi, [out_ptr]
    lea     rax, [rdi+rcx]
    cmp     rax, [out_end]
    ja      .err
    rep movsb
    mov     [in_ptr], rsi
    mov     [out_ptr], rdi
    jmp     .after

.fixed:
    lea     rdi, [cl_len]
    xor     eax, eax
.fl:
    mov     ecx, 8
    cmp     eax, 144
    jb      .fs
    mov     ecx, 9
    cmp     eax, 256
    jb      .fs
    mov     ecx, 7
    cmp     eax, 280
    jb      .fs
    mov     ecx, 8
.fs:
    mov     [rdi+rax*4], ecx
    inc     eax
    cmp     eax, 288
    jb      .fl
    lea     rdi, [lencnt]
    lea     rsi, [lensym]
    lea     rdx, [cl_len]
    mov     ecx, 288
    call    construct
    lea     rdi, [cl_len]
    xor     eax, eax
.fd:
    mov     dword [rdi+rax*4], 5
    inc     eax
    cmp     eax, 30
    jb      .fd
    lea     rdi, [distcnt]
    lea     rsi, [distsym]
    lea     rdx, [cl_len]
    mov     ecx, 30
    call    construct
    call    codes
    test    eax, eax
    jnz     .err
    jmp     .after

.dynamic:
    mov     edi, 5
    call    getbits
    lea     r13d, [rax+257]         ; nlen
    mov     edi, 5
    call    getbits
    lea     r14d, [rax+1]           ; ndist
    mov     edi, 4
    call    getbits
    lea     r15d, [rax+4]           ; ncode
    cmp     r13d, 286
    ja      .err
    cmp     r14d, 30
    ja      .err
    lea     rdi, [cl_len]
    xor     eax, eax
.z19:
    mov     dword [rdi+rax*4], 0
    inc     eax
    cmp     eax, 19
    jb      .z19
    xor     ebx, ebx
.rc:
    mov     edi, 3
    call    getbits
    lea     rcx, [clorder]
    movzx   edx, byte [rcx+rbx]
    lea     rcx, [cl_len]
    mov     [rcx+rdx*4], eax
    inc     ebx
    cmp     ebx, r15d
    jb      .rc
    lea     rdi, [lencnt]
    lea     rsi, [lensym]
    lea     rdx, [cl_len]
    mov     ecx, 19
    call    construct
    ; read nlen + ndist code lengths
    xor     ebx, ebx                ; index
    lea     r15d, [r13+r14]
.rl:
    cmp     ebx, r15d
    jae     .rl_done
    lea     rdi, [lencnt]
    lea     rsi, [lensym]
    call    decode
    test    eax, eax
    js      .err
    cmp     eax, 16
    jae     .rep
    lea     rcx, [cl_len]
    mov     [rcx+rbx*4], eax
    inc     ebx
    jmp     .rl
.rep:
    xor     r12d, r12d              ; value to repeat
    cmp     eax, 16
    jne     .r17
    test    ebx, ebx
    jz      .err
    lea     rcx, [cl_len]
    mov     r12d, [rcx+rbx*4-4]
    mov     edi, 2
    call    getbits
    add     eax, 3
    jmp     .rfill
.r17:
    cmp     eax, 17
    jne     .r18
    mov     edi, 3
    call    getbits
    add     eax, 3
    jmp     .rfill
.r18:
    mov     edi, 7
    call    getbits
    add     eax, 11
.rfill:
    lea     edx, [rbx+rax]
    cmp     edx, r15d
    ja      .err
    lea     rcx, [cl_len]
.rf:
    mov     [rcx+rbx*4], r12d
    inc     ebx
    dec     eax
    jnz     .rf
    jmp     .rl
.rl_done:
    lea     rdi, [lencnt]
    lea     rsi, [lensym]
    lea     rdx, [cl_len]
    mov     ecx, r13d
    call    construct
    lea     rdi, [distcnt]
    lea     rsi, [distsym]
    lea     rdx, [cl_len]
    lea     rdx, [rdx+r13*4]
    mov     ecx, r14d
    call    construct
    call    codes
    test    eax, eax
    jnz     .err

.after:
    cmp     dword [inf_err], 0
    jne     .err
    cmp     dword [inf_last], 0
    je      .block
.ok:
    xor     eax, eax
    cmp     dword [inf_err], 0
    je      .out
.err:
    mov     eax, 1
.out:
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
    ret

; =====================================================================
;  PNG decoder: 8-bit RGB / RGBA, non-interlaced -> tex_rgba (RGBA)
;  rdi data, esi length -> eax 1 ok, png_w/png_h set
; =====================================================================
png_decode:
    push    rbp
    mov     rbp, rsp
    push    rbx
    push    r12
    push    r13
    push    r14
    push    r15
    sub     rsp, 24
    mov     r12, rdi
    mov     eax, esi
    lea     r13, [rdi+rax]          ; end
    cmp     esi, 8
    jb      .fail
    mov     rax, [r12]
    mov     rdx, 0x0A1A0A0D474E5089 ; \x89PNG\r\n\x1a\n
    cmp     rax, rdx
    jne     .fail
    lea     rbx, [r12+8]
    xor     r14d, r14d              ; IDAT bytes collected
    mov     dword [png_w], 0
.chunk:
    lea     rax, [rbx+12]
    cmp     rax, r13
    ja      .fail
    mov     eax, [rbx]
    bswap   eax
    mov     r15d, eax               ; chunk length
    lea     rdx, [rbx+12]
    add     rdx, r15
    cmp     rdx, r13
    ja      .fail
    mov     eax, [rbx+4]
    cmp     eax, 'IHDR'
    je      .ihdr
    cmp     eax, 'IDAT'
    je      .idat
    cmp     eax, 'IEND'
    je      .iend
    jmp     .adv
.ihdr:
    mov     eax, [rbx+8]
    bswap   eax
    mov     [png_w], eax
    mov     eax, [rbx+12]
    bswap   eax
    mov     [png_h], eax
    cmp     byte [rbx+16], 8        ; bit depth
    jne     .fail
    cmp     byte [rbx+20], 0        ; interlace
    jne     .fail
    mov     dword [png_bpp], 4
    cmp     byte [rbx+17], 6        ; RGBA
    je      .dims
    mov     dword [png_bpp], 3
    cmp     byte [rbx+17], 2        ; RGB
    jne     .fail
.dims:
    mov     eax, [png_w]
    dec     eax
    cmp     eax, PNGMAXDIM
    jae     .fail
    mov     eax, [png_h]
    dec     eax
    cmp     eax, PNGMAXDIM
    jae     .fail
    jmp     .adv
.idat:
    lea     eax, [r14+r15]
    cmp     eax, IDATMAX
    ja      .fail
    lea     rdi, [idat_buf]
    add     rdi, r14
    lea     rsi, [rbx+8]
    mov     ecx, r15d
    rep movsb
    add     r14d, r15d
.adv:
    lea     rbx, [rbx+r15+12]
    jmp     .chunk
.iend:
    cmp     dword [png_w], 0
    je      .fail
    cmp     r14d, 6
    jb      .fail
    lea     rax, [idat_buf+2]       ; skip zlib header
    mov     [in_ptr], rax
    lea     rax, [idat_buf]
    add     rax, r14
    mov     [in_end], rax
    mov     eax, [png_w]
    imul    eax, [png_bpp]
    mov     r14d, eax               ; stride
    inc     eax
    imul    eax, [png_h]            ; total raw size
    lea     rdi, [raw_buf]
    mov     [out_ptr], rdi
    mov     [out_start], rdi
    add     rdi, rax
    mov     [out_end], rdi
    call    inflate
    test    eax, eax
    jnz     .fail
    mov     rax, [out_ptr]
    cmp     rax, [out_end]
    jne     .fail

    ; ---- undo row filters in place ----
    lea     r12, [raw_buf]          ; row start (filter byte)
    xor     ebx, ebx                ; row
    mov     r15d, [png_bpp]
.row:
    movzx   r13d, byte [r12]        ; filter type
    cmp     r13d, 4
    ja      .fail
    lea     r8, [r12+1]             ; current row
    mov     r9, r8
    sub     r9, r14
    dec     r9                      ; previous row
    xor     ecx, ecx
.px:
    xor     r10d, r10d              ; a (left)
    xor     r11d, r11d              ; b (up)
    xor     edx, edx                ; c (up-left)
    cmp     ecx, r15d
    jb      .noa
    mov     eax, ecx
    sub     eax, r15d
    movzx   r10d, byte [r8+rax]
    test    ebx, ebx
    jz      .noa
    movzx   edx, byte [r9+rax]
.noa:
    test    ebx, ebx
    jz      .nob
    movzx   r11d, byte [r9+rcx]
.nob:
    movzx   eax, byte [r8+rcx]
    cmp     r13d, 1
    je      .f1
    cmp     r13d, 2
    je      .f2
    cmp     r13d, 3
    je      .f3
    cmp     r13d, 4
    je      .f4
    jmp     .st
.f1:
    add     eax, r10d
    jmp     .st
.f2:
    add     eax, r11d
    jmp     .st
.f3:
    mov     esi, r10d
    add     esi, r11d
    shr     esi, 1
    add     eax, esi
    jmp     .st
.f4:                                ; Paeth predictor
    push    rax
    mov     eax, r10d
    add     eax, r11d
    sub     eax, edx                ; p
    mov     esi, eax
    sub     esi, r10d
    mov     edi, esi
    sar     edi, 31
    xor     esi, edi
    sub     esi, edi                ; pa
    mov     edi, eax
    sub     edi, r11d
    push    rdx
    mov     edx, edi
    sar     edx, 31
    xor     edi, edx
    sub     edi, edx                ; pb
    pop     rdx
    sub     eax, edx
    push    rdx
    mov     edx, eax
    sar     edx, 31
    xor     eax, edx
    sub     eax, edx                ; pc
    pop     rdx
    cmp     esi, edi
    ja      .pb
    cmp     esi, eax
    ja      .pb
    mov     esi, r10d
    jmp     .pd
.pb:
    cmp     edi, eax
    ja      .pc
    mov     esi, r11d
    jmp     .pd
.pc:
    mov     esi, edx
.pd:
    pop     rax
    add     eax, esi
.st:
    mov     [r8+rcx], al
    inc     ecx
    cmp     ecx, r14d
    jb      .px
    lea     r12, [r12+r14+1]
    inc     ebx
    cmp     ebx, [png_h]
    jb      .row

    ; ---- copy rows into tightly packed RGBA ----
    lea     rsi, [raw_buf]
    lea     rdi, [tex_rgba]
    xor     ebx, ebx
.crow:
    inc     rsi                     ; skip filter byte
    mov     ecx, [png_w]
.cpx:
    mov     al, [rsi]
    mov     [rdi], al
    mov     al, [rsi+1]
    mov     [rdi+1], al
    mov     al, [rsi+2]
    mov     [rdi+2], al
    mov     al, 255
    cmp     r15d, 4
    jne     .na
    mov     al, [rsi+3]
.na:
    mov     [rdi+3], al
    add     rsi, r15
    add     rdi, 4
    dec     ecx
    jnz     .cpx
    inc     ebx
    cmp     ebx, [png_h]
    jb      .crow
    mov     eax, 1
    jmp     .out
.fail:
    xor     eax, eax
.out:
    add     rsp, 24
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
    pop     rbp
    ret

; load_textures: textures.png from disk if present, else the embedded copy
load_textures:
    push    rbp
    mov     rbp, rsp
    push    rbx
    push    r12
    mov     eax, 2                  ; open
    lea     rdi, [s_texfile]
    xor     esi, esi
    syscall
    test    eax, eax
    js      .embedded
    mov     r12d, eax
    xor     ebx, ebx
.rd:
    mov     edx, FILEMAX
    sub     edx, ebx
    jz      .rdone
    xor     eax, eax                ; read
    mov     edi, r12d
    lea     rsi, [file_buf]
    add     rsi, rbx
    syscall
    test    rax, rax
    jle     .rdone
    add     ebx, eax
    jmp     .rd
.rdone:
    mov     eax, 3                  ; close
    mov     edi, r12d
    syscall
    lea     rdi, [file_buf]
    mov     esi, ebx
    call    png_decode
    test    eax, eax
    jnz     .upload
.embedded:
    lea     rdi, [png_embedded]
    mov     esi, png_embedded_len
    call    png_decode
    test    eax, eax
    jnz     .upload
    lea     rdi, [s_notex]
    call    puts
    jmp     .out
.upload:
    mov     edi, 1
    lea     rsi, [tex_id]
    call    glGenTextures
    mov     edi, GL_TEXTURE_2D
    mov     esi, [tex_id]
    call    glBindTexture
    mov     edi, GL_TEXTURE_2D
    mov     esi, GL_TEX_MIN
    mov     edx, GL_NEAREST
    call    glTexParameteri
    mov     edi, GL_TEXTURE_2D
    mov     esi, GL_TEX_MAG
    mov     edx, GL_NEAREST
    call    glTexParameteri
    sub     rsp, 8
    lea     rax, [tex_rgba]
    push    rax                     ; pixels
    push    GL_UNSIGNED_BYTE        ; type
    push    GL_RGBA                 ; format
    mov     edi, GL_TEXTURE_2D
    xor     esi, esi
    mov     edx, GL_RGBA
    mov     ecx, [png_w]
    mov     r8d, [png_h]
    xor     r9d, r9d
    call    glTexImage2D
    add     rsp, 32
.out:
    pop     r12
    pop     rbx
    pop     rbp
    ret

; =====================================================================
;  player physics
; =====================================================================
collides:                           ; xmm0 px, xmm1 py, xmm2 pz -> eax
    push    rbp
    mov     rbp, rsp
    push    rbx
    push    r12
    push    r13
    push    r14
    push    r15
    sub     rsp, 40
    movss   [rsp+24], xmm0
    movss   [rsp+28], xmm1
    movss   [rsp+32], xmm2
    movss   xmm0, [rsp+24]
    subss   xmm0, [f_pw]
    call    ffloor
    mov     [rsp], eax
    movss   xmm0, [rsp+24]
    addss   xmm0, [f_pw]
    subss   xmm0, [f_eps]
    call    ffloor
    mov     [rsp+4], eax
    movss   xmm0, [rsp+28]
    call    ffloor
    mov     [rsp+8], eax
    movss   xmm0, [rsp+28]
    addss   xmm0, [f_ph]
    subss   xmm0, [f_eps]
    call    ffloor
    mov     [rsp+12], eax
    movss   xmm0, [rsp+32]
    subss   xmm0, [f_pw]
    call    ffloor
    mov     [rsp+16], eax
    movss   xmm0, [rsp+32]
    addss   xmm0, [f_pw]
    subss   xmm0, [f_eps]
    call    ffloor
    mov     [rsp+20], eax
    mov     r12d, [rsp+8]
.ly:
    mov     r13d, [rsp+16]
.lz:
    mov     r14d, [rsp]
.lx:
    mov     edi, r14d
    mov     esi, r12d
    mov     edx, r13d
    call    solid_at
    test    eax, eax
    jnz     .yes
    inc     r14d
    cmp     r14d, [rsp+4]
    jle     .lx
    inc     r13d
    cmp     r13d, [rsp+20]
    jle     .lz
    inc     r12d
    cmp     r12d, [rsp+12]
    jle     .ly
    xor     eax, eax
    jmp     .out
.yes:
    mov     eax, 1
.out:
    add     rsp, 40
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
    pop     rbp
    ret

update_player:                      ; xmm0 = dt
    push    rbp
    mov     rbp, rsp
    sub     rsp, 32
    movss   [rsp], xmm0
    movss   xmm0, [yaw]
    call    sincos
    movss   [rsp+4], xmm0
    movss   [rsp+8], xmm1
    xorps   xmm2, xmm2
    xorps   xmm3, xmm3
    cmp     byte [locked], 0
    je      .noin
    movzx   eax, byte [key_w]
    movzx   ecx, byte [key_s]
    sub     eax, ecx
    cvtsi2ss xmm2, eax
    movzx   eax, byte [key_d]
    movzx   ecx, byte [key_a]
    sub     eax, ecx
    cvtsi2ss xmm3, eax
.noin:
    movss   xmm4, xmm2
    mulss   xmm4, [rsp+4]
    movss   xmm5, xmm3
    mulss   xmm5, [rsp+8]
    subss   xmm5, xmm4
    movss   xmm6, xmm2
    mulss   xmm6, [rsp+8]
    movss   xmm7, xmm3
    mulss   xmm7, [rsp+4]
    addss   xmm6, xmm7
    xorps   xmm6, [signmask4]
    movss   xmm0, xmm5
    mulss   xmm0, xmm5
    movss   xmm1, xmm6
    mulss   xmm1, xmm6
    addss   xmm0, xmm1
    comiss  xmm0, [f_tiny]
    jb      .nomove
    sqrtss  xmm0, xmm0
    movss   xmm1, [f_speed]
    mulss   xmm1, [rsp]
    divss   xmm1, xmm0
    mulss   xmm5, xmm1
    mulss   xmm6, xmm1
    jmp     .mv
.nomove:
    xorps   xmm5, xmm5
    xorps   xmm6, xmm6
.mv:
    movss   [rsp+12], xmm5
    movss   [rsp+16], xmm6
    movss   xmm0, [vy]
    movss   xmm1, [f_grav]
    mulss   xmm1, [rsp]
    subss   xmm0, xmm1
    maxss   xmm0, [f_minvy]
    cmp     byte [locked], 0
    je      .nojump
    cmp     byte [key_sp], 0
    je      .nojump
    cmp     byte [on_ground], 0
    je      .nojump
    movss   xmm0, [f_jump]
.nojump:
    movss   [vy], xmm0
    movss   xmm0, [px]
    addss   xmm0, [rsp+12]
    movss   [rsp+20], xmm0
    movss   xmm1, [py]
    movss   xmm2, [pz]
    call    collides
    test    eax, eax
    jnz     .zax
    movss   xmm0, [rsp+20]
    movss   [px], xmm0
.zax:
    movss   xmm2, [pz]
    addss   xmm2, [rsp+16]
    movss   [rsp+20], xmm2
    movss   xmm0, [px]
    movss   xmm1, [py]
    call    collides
    test    eax, eax
    jnz     .yax
    movss   xmm0, [rsp+20]
    movss   [pz], xmm0
.yax:
    mov     byte [on_ground], 0
    movss   xmm1, [vy]
    mulss   xmm1, [rsp]
    addss   xmm1, [py]
    movss   [rsp+20], xmm1
    movss   xmm0, [px]
    movss   xmm2, [pz]
    call    collides
    test    eax, eax
    jz      .ymove
    xorps   xmm0, xmm0
    comiss  xmm0, [vy]
    jbe     .ceil
    movss   xmm0, [rsp+20]
    call    ffloor
    inc     eax
    cvtsi2ss xmm0, eax
    movss   [py], xmm0
    mov     byte [on_ground], 1
.ceil:
    mov     dword [vy], 0
    jmp     .done
.ymove:
    movss   xmm0, [rsp+20]
    movss   [py], xmm0
.done:
    leave
    ret

spawn:                              ; first grass-topped column from the centre
    push    rbp
    mov     rbp, rsp
    push    rbx
    push    r12
    mov     r12d, 40
.col:
    mov     ebx, WY-1
.l:
    mov     edi, r12d
    mov     esi, ebx
    mov     edx, 40
    call    get_block
    test    eax, eax
    jnz     .found
    dec     ebx
    jns     .l
.found:
    cmp     eax, GRASS
    je      .ok
    inc     r12d
    cmp     r12d, WX-1
    jb      .col
.ok:
    cvtsi2ss xmm0, r12d
    addss   xmm0, [f_half]
    movss   [px], xmm0
    inc     ebx
    cvtsi2ss xmm0, ebx
    movss   [py], xmm0
    pop     r12
    pop     rbx
    pop     rbp
    ret

; =====================================================================
;  raycast (voxel DDA), reach 5 blocks
; =====================================================================
raycast:
    push    rbp
    mov     rbp, rsp
    push    rbx
    push    r12
    movss   xmm0, [pitch]
    call    sincos
    movss   [ray_d+4], xmm0
    movss   [tmp_cp], xmm1
    movss   xmm0, [yaw]
    call    sincos
    mulss   xmm0, [tmp_cp]
    xorps   xmm0, [signmask4]
    movss   [ray_d], xmm0
    mulss   xmm1, [tmp_cp]
    xorps   xmm1, [signmask4]
    movss   [ray_d+8], xmm1
    movss   xmm0, [px]
    movss   [ray_o], xmm0
    movss   xmm0, [py]
    addss   xmm0, [f_eye]
    movss   [ray_o+4], xmm0
    movss   xmm0, [pz]
    movss   [ray_o+8], xmm0
    xor     ebx, ebx
.init:
    lea     rcx, [ray_o]
    movss   xmm0, [rcx+rbx*4]
    roundss xmm1, xmm0, 9
    cvttss2si eax, xmm1
    lea     rcx, [cell]
    mov     [rcx+rbx*4], eax
    lea     rcx, [prev]
    mov     [rcx+rbx*4], eax
    lea     rcx, [ray_d]
    movss   xmm2, [rcx+rbx*4]
    movss   xmm3, xmm2
    mulss   xmm3, xmm3
    sqrtss  xmm3, xmm3
    lea     rdx, [rstep]
    comiss  xmm3, [f_tiny]
    jb      .zero
    movss   xmm4, [f_one]
    divss   xmm4, xmm3
    xorps   xmm6, xmm6
    comiss  xmm2, xmm6
    ja      .pos
    mov     dword [rdx+rbx*4], -1
    movss   xmm5, xmm0
    subss   xmm5, xmm1
    mulss   xmm5, xmm4
    jmp     .store
.pos:
    mov     dword [rdx+rbx*4], 1
    movss   xmm5, xmm1
    addss   xmm5, [f_one]
    subss   xmm5, xmm0
    mulss   xmm5, xmm4
    jmp     .store
.zero:
    mov     dword [rdx+rbx*4], 0
    movss   xmm4, [f_big]
    movss   xmm5, xmm4
.store:
    lea     rcx, [tmax]
    movss   [rcx+rbx*4], xmm5
    lea     rcx, [tdelta]
    movss   [rcx+rbx*4], xmm4
    inc     ebx
    cmp     ebx, 3
    jb      .init
    mov     dword [hit_flag], 0
    mov     r12d, 64
.loop:
    mov     edi, [cell]
    mov     esi, [cell+4]
    mov     edx, [cell+8]
    call    get_block
    test    eax, eax
    jnz     .hit
    xor     ebx, ebx
    movss   xmm0, [tmax]
    movss   xmm1, [tmax+4]
    comiss  xmm1, xmm0
    jae     .c2
    mov     ebx, 1
    movss   xmm0, xmm1
.c2:
    movss   xmm1, [tmax+8]
    comiss  xmm1, xmm0
    jae     .c3
    mov     ebx, 2
    movss   xmm0, xmm1
.c3:
    comiss  xmm0, [f_reach]
    ja      .done
    mov     eax, [cell]
    mov     [prev], eax
    mov     eax, [cell+4]
    mov     [prev+4], eax
    mov     eax, [cell+8]
    mov     [prev+8], eax
    lea     rcx, [rstep]
    mov     eax, [rcx+rbx*4]
    lea     rcx, [cell]
    add     [rcx+rbx*4], eax
    lea     rcx, [tdelta]
    addss   xmm0, [rcx+rbx*4]
    lea     rcx, [tmax]
    movss   [rcx+rbx*4], xmm0
    dec     r12d
    jnz     .loop
    jmp     .done
.hit:
    mov     dword [hit_flag], 1
    mov     eax, [cell]
    mov     [hit], eax
    mov     eax, [cell+4]
    mov     [hit+4], eax
    mov     eax, [cell+8]
    mov     [hit+8], eax
.done:
    pop     r12
    pop     rbx
    pop     rbp
    ret

; =====================================================================
;  inventory
; =====================================================================
; add_item: edi id -> merges into an existing stack or the first empty slot
add_item:
    lea     rsi, [inv_id]
    lea     rdx, [inv_cnt]
    xor     ecx, ecx
.m:
    movzx   eax, byte [rsi+rcx]
    cmp     eax, edi
    jne     .mn
    cmp     byte [rdx+rcx], STACK
    jae     .mn
    inc     byte [rdx+rcx]
    ret
.mn:
    inc     ecx
    cmp     ecx, NSLOTS
    jb      .m
    xor     ecx, ecx
.e:
    cmp     byte [rsi+rcx], 0
    jne     .en
    mov     [rsi+rcx], dil
    mov     byte [rdx+rcx], 1
    ret
.en:
    inc     ecx
    cmp     ecx, NSLOTS
    jb      .e
    ret                             ; inventory full: item is lost

; inv_layout -> r8d grid x, r9d grid y (top-left of the 9x3 grid)
inv_layout:
    mov     r8d, [win_w]
    sub     r8d, 9*SSTEP+20
    sar     r8d, 1
    add     r8d, 12
    mov     r9d, [win_h]
    sub     r9d, 30+4*SSTEP+14+14
    sar     r9d, 1
    add     r9d, 30
    ret

; slot_pos: edi slot -> eax x, edx y (inventory screen)
slot_pos:
    call    inv_layout
    cmp     edi, 9
    jae     .main
    imul    eax, edi, SSTEP
    add     eax, r8d
    lea     edx, [r9+3*SSTEP+14]
    ret
.main:
    lea     eax, [rdi-9]
    xor     edx, edx
    mov     ecx, 9
    div     ecx                     ; eax row, edx column
    imul    ecx, edx, SSTEP
    imul    edx, eax, SSTEP
    add     edx, r9d
    lea     eax, [rcx+r8]
    ret

; slot_at_mouse -> eax slot index or -1
slot_at_mouse:
    push    rbx
    xor     ebx, ebx
.l:
    mov     edi, ebx
    call    slot_pos
    mov     ecx, [mouse_x]
    sub     ecx, eax
    cmp     ecx, SLOT
    jae     .n
    mov     ecx, [mouse_y]
    sub     ecx, edx
    cmp     ecx, SLOT
    jae     .n
    mov     eax, ebx
    pop     rbx
    ret
.n:
    inc     ebx
    cmp     ebx, NSLOTS
    jb      .l
    mov     eax, -1
    pop     rbx
    ret

; inv_click: edi button (1 left, 3 right)
inv_click:
    push    rbx
    push    r12
    sub     rsp, 8
    mov     r12d, edi
    call    slot_at_mouse
    test    eax, eax
    js      .out
    mov     ebx, eax
    lea     rsi, [inv_id]
    lea     rdx, [inv_cnt]
    cmp     r12d, 3
    je      .right
    ; ---- left: take / put / merge / swap ----
    cmp     byte [held_cnt], 0
    jne     .lhave
    mov     al, [rsi+rbx]
    mov     [held_id], al
    mov     al, [rdx+rbx]
    mov     [held_cnt], al
    mov     byte [rsi+rbx], 0
    mov     byte [rdx+rbx], 0
    jmp     .out
.lhave:
    cmp     byte [rdx+rbx], 0
    je      .swap                   ; empty slot: swapping == putting
    mov     al, [rsi+rbx]
    cmp     al, [held_id]
    jne     .swap
    movzx   eax, byte [rdx+rbx]     ; merge
    mov     ecx, STACK
    sub     ecx, eax                ; room
    movzx   eax, byte [held_cnt]
    cmp     eax, ecx
    jbe     .mall
    mov     eax, ecx
.mall:
    add     [rdx+rbx], al
    sub     [held_cnt], al
    jnz     .out
    mov     byte [held_id], 0
    jmp     .out
.swap:
    mov     al, [rsi+rbx]
    mov     cl, [held_id]
    mov     [held_id], al
    mov     [rsi+rbx], cl
    mov     al, [rdx+rbx]
    mov     cl, [held_cnt]
    mov     [held_cnt], al
    mov     [rdx+rbx], cl
    jmp     .out
    ; ---- right: take half / put one ----
.right:
    cmp     byte [held_cnt], 0
    jne     .rhave
    movzx   eax, byte [rdx+rbx]
    test    eax, eax
    jz      .out
    inc     eax
    shr     eax, 1                  ; ceil(n/2)
    mov     [held_cnt], al
    mov     cl, [rsi+rbx]
    mov     [held_id], cl
    sub     [rdx+rbx], al
    jnz     .out
    mov     byte [rsi+rbx], 0
    jmp     .out
.rhave:
    cmp     byte [rdx+rbx], 0
    jne     .rsame
    mov     al, [held_id]
    mov     [rsi+rbx], al
    jmp     .rone
.rsame:
    mov     al, [rsi+rbx]
    cmp     al, [held_id]
    jne     .out
    cmp     byte [rdx+rbx], STACK
    jae     .out
.rone:
    inc     byte [rdx+rbx]
    dec     byte [held_cnt]
    jnz     .out
    mov     byte [held_id], 0
.out:
    add     rsp, 8
    pop     r12
    pop     rbx
    ret

open_inventory:
    push    rbp
    mov     rbp, rsp
    call    unlock_mouse
    mov     byte [inv_open], 1
    mov     eax, [win_w]
    shr     eax, 1
    mov     [mouse_x], eax
    mov     eax, [win_h]
    shr     eax, 1
    mov     [mouse_y], eax
    pop     rbp
    ret

close_inventory:                    ; put the held stack back, return to game
    push    rbp
    mov     rbp, rsp
    mov     byte [inv_open], 0
.back:
    cmp     byte [held_cnt], 0
    je      .done
    movzx   edi, byte [held_id]
    call    add_item
    dec     byte [held_cnt]
    jmp     .back
.done:
    mov     byte [held_id], 0
    call    lock_mouse
    pop     rbp
    ret

; =====================================================================
;  block editing
; =====================================================================
break_block:
    push    rbp
    mov     rbp, rsp
    push    rbx
    sub     rsp, 8
    call    raycast
    cmp     dword [hit_flag], 0
    je      .out
    cmp     dword [hit+4], 0        ; bedrock layer is unbreakable
    je      .out
    mov     edi, [hit]
    mov     esi, [hit+4]
    mov     edx, [hit+8]
    call    get_block
    mov     ebx, eax
    cmp     ebx, BEDROCK
    je      .out
    mov     edi, [hit]
    mov     esi, [hit+4]
    mov     edx, [hit+8]
    xor     ecx, ecx
    call    set_block
    mov     edi, ebx
    cmp     edi, GRASS              ; grass drops dirt
    jne     .drop
    mov     edi, DIRT
.drop:
    call    add_item
    mov     edi, [hit]
    mov     esi, [hit+8]
    call    rebuild_around
.out:
    add     rsp, 8
    pop     rbx
    pop     rbp
    ret

place_block:
    push    rbp
    mov     rbp, rsp
    push    rbx
    push    r12
    push    r13
    push    r14
    mov     ebx, [sel]
    lea     rax, [inv_cnt]
    cmp     byte [rax+rbx], 0
    je      .out
    call    raycast
    cmp     dword [hit_flag], 0
    je      .out
    mov     r12d, [prev]
    mov     r13d, [prev+4]
    mov     r14d, [prev+8]
    cmp     r12d, [hit]
    jne     .ok
    cmp     r13d, [hit+4]
    jne     .ok
    cmp     r14d, [hit+8]
    je      .out
.ok:
    cmp     r12d, WX
    jae     .out
    cmp     r13d, WY
    jae     .out
    cmp     r14d, WZ
    jae     .out
    mov     edi, r12d
    mov     esi, r13d
    mov     edx, r14d
    call    get_block
    test    eax, eax
    jnz     .out
    mov     edi, r12d
    mov     esi, r13d
    mov     edx, r14d
    lea     rax, [inv_id]
    movzx   ecx, byte [rax+rbx]
    call    set_block
    movss   xmm0, [px]
    movss   xmm1, [py]
    movss   xmm2, [pz]
    call    collides
    test    eax, eax
    jz      .commit
    mov     edi, r12d
    mov     esi, r13d
    mov     edx, r14d
    xor     ecx, ecx
    call    set_block
    jmp     .out
.commit:
    lea     rax, [inv_cnt]
    dec     byte [rax+rbx]
    jnz     .rb
    lea     rax, [inv_id]
    mov     byte [rax+rbx], 0
.rb:
    mov     edi, r12d
    mov     esi, r14d
    call    rebuild_around
.out:
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
    pop     rbp
    ret

; =====================================================================
;  mouse capture
; =====================================================================
warp_center:
    push    rbp
    mov     rbp, rsp
    sub     rsp, 8
    mov     eax, [win_h]
    shr     eax, 1
    push    rax
    mov     eax, [win_w]
    shr     eax, 1
    push    rax
    push    0
    mov     rdi, [dpy]
    xor     esi, esi
    mov     rdx, [win]
    xor     ecx, ecx
    xor     r8d, r8d
    xor     r9d, r9d
    call    XWarpPointer
    leave
    ret

lock_mouse:
    push    rbp
    mov     rbp, rsp
    sub     rsp, 8
    push    0
    push    qword [blank_cursor]
    push    qword [win]
    mov     rdi, [dpy]
    mov     rsi, [win]
    mov     edx, 1
    mov     ecx, 0x4C
    mov     r8d, 1
    mov     r9d, 1
    call    XGrabPointer
    mov     rdi, [dpy]
    mov     rsi, [win]
    mov     rdx, [blank_cursor]
    call    XDefineCursor
    mov     byte [locked], 1
    call    warp_center
    leave
    ret

unlock_mouse:
    push    rbp
    mov     rbp, rsp
    cmp     byte [locked], 0
    je      .out
    mov     byte [locked], 0
    mov     qword [keys], 0
    mov     rdi, [dpy]
    xor     esi, esi
    call    XUngrabPointer
    mov     rdi, [dpy]
    mov     rsi, [win]
    call    XUndefineCursor
.out:
    pop     rbp
    ret

; =====================================================================
;  input
; =====================================================================
handle_key:                         ; edi keysym, esi 1 down / 0 up
    cmp     edi, 0xff1b             ; Escape
    jne     .notesc
    test    esi, esi
    jz      .ret
    cmp     byte [inv_open], 0
    jne     close_inventory
    jmp     unlock_mouse
.notesc:
    or      edi, 0x20
    cmp     edi, 'e'
    jne     .k0
    test    esi, esi
    jz      .ret
    cmp     byte [inv_open], 0
    jne     close_inventory
    jmp     open_inventory
.k0:
    cmp     edi, 'w'
    jne     .k1
    mov     [key_w], sil
    ret
.k1:
    cmp     edi, 'a'
    jne     .k2
    mov     [key_a], sil
    ret
.k2:
    cmp     edi, 's'
    jne     .k3
    mov     [key_s], sil
    ret
.k3:
    cmp     edi, 'd'
    jne     .k4
    mov     [key_d], sil
    ret
.k4:
    cmp     edi, ' '
    jne     .k5
    mov     [key_sp], sil
    ret
.k5:
    test    esi, esi
    jz      .ret
    lea     eax, [rdi-'1']
    cmp     eax, 8
    ja      .ret
    mov     [sel], eax
.ret:
    ret

handle_button:                      ; edi button
    cmp     byte [inv_open], 0
    je      .game
    mov     eax, [xev+64]
    mov     [mouse_x], eax
    mov     eax, [xev+68]
    mov     [mouse_y], eax
    cmp     edi, 1
    je      inv_click
    cmp     edi, 3
    je      inv_click
    ret
.game:
    cmp     byte [locked], 0
    jne     .act
    jmp     lock_mouse
.act:
    cmp     edi, 1
    je      break_block
    cmp     edi, 3
    je      place_block
    cmp     edi, 4
    jne     .b5
    mov     eax, [sel]
    add     eax, 8
    jmp     .mod
.b5:
    cmp     edi, 5
    jne     .ret
    mov     eax, [sel]
    inc     eax
.mod:
    xor     edx, edx
    mov     ecx, 9
    div     ecx
    mov     [sel], edx
.ret:
    ret

handle_motion:
    cmp     byte [locked], 0
    jne     .look
    mov     eax, [xev+64]
    mov     [mouse_x], eax
    mov     eax, [xev+68]
    mov     [mouse_y], eax
    ret
.look:
    mov     eax, [xev+64]
    mov     ecx, [win_w]
    shr     ecx, 1
    sub     eax, ecx
    mov     edx, [xev+68]
    mov     ecx, [win_h]
    shr     ecx, 1
    sub     edx, ecx
    mov     ecx, eax
    or      ecx, edx
    jz      .ret
    cvtsi2ss xmm0, eax
    mulss   xmm0, [f_sens]
    movss   xmm1, [yaw]
    subss   xmm1, xmm0
    movss   [yaw], xmm1
    cvtsi2ss xmm0, edx
    mulss   xmm0, [f_sens]
    movss   xmm1, [pitch]
    subss   xmm1, xmm0
    maxss   xmm1, [f_pmin]
    minss   xmm1, [f_pmax]
    movss   [pitch], xmm1
    jmp     warp_center
.ret:
    ret

pump_events:
    push    rbp
    mov     rbp, rsp
    push    rbx
    sub     rsp, 8
.next:
    mov     rdi, [dpy]
    call    XPending
    test    eax, eax
    jz      .done
    mov     rdi, [dpy]
    lea     rsi, [xev]
    call    XNextEvent
    mov     eax, [xev]
    cmp     eax, KeyPress
    je      .kp
    cmp     eax, KeyRelease
    je      .kr
    cmp     eax, ButtonPress
    je      .bp
    cmp     eax, MotionNotify
    je      .mo
    cmp     eax, FocusOut
    je      .fo
    cmp     eax, ConfigureNotify
    je      .cf
    cmp     eax, ClientMessage
    je      .cm
    jmp     .next
.kp:
    lea     rdi, [xev]
    xor     esi, esi
    call    XLookupKeysym
    mov     edi, eax
    mov     esi, 1
    call    handle_key
    jmp     .next
.kr:
    lea     rdi, [xev]
    xor     esi, esi
    call    XLookupKeysym
    mov     edi, eax
    xor     esi, esi
    call    handle_key
    jmp     .next
.bp:
    mov     edi, [xev+84]
    call    handle_button
    jmp     .next
.mo:
    call    handle_motion
    jmp     .next
.fo:
    cmp     dword [xev+40], 0
    jne     .next
    call    unlock_mouse
    jmp     .next
.cf:
    mov     eax, [xev+56]
    mov     [win_w], eax
    mov     eax, [xev+60]
    mov     [win_h], eax
    xor     edi, edi
    xor     esi, esi
    mov     edx, [win_w]
    mov     ecx, [win_h]
    call    glViewport
    jmp     .next
.cm:
    mov     rax, [xev+56]
    cmp     rax, [wm_delete]
    jne     .next
    mov     byte [running], 0
    jmp     .next
.done:
    add     rsp, 8
    pop     rbx
    pop     rbp
    ret

; =====================================================================
;  HUD helpers
; =====================================================================
rect:                               ; edi x, esi y, edx w, ecx h
    push    rbp
    mov     rbp, rsp
    add     edx, edi
    add     ecx, esi
    call    glRecti
    pop     rbp
    ret

color:                              ; edi = 0xAABBGGRR
    push    rbp
    mov     rbp, rsp
    mov     eax, edi
    movzx   edi, al
    movzx   esi, ah
    shr     eax, 16
    movzx   edx, al
    movzx   ecx, ah
    call    glColor4ub
    pop     rbp
    ret

strlen_:
    xor     eax, eax
.l:
    cmp     byte [rdi+rax], 0
    je      .e
    inc     eax
    jmp     .l
.e:
    ret

text:                               ; edi x, esi y (baseline), rdx string
    push    rbp
    mov     rbp, rsp
    push    rbx
    push    r12
    push    r13
    push    r14
    cmp     dword [font_base], 0
    je      .out
    mov     r12d, edi
    mov     r13d, esi
    mov     r14, rdx
    mov     rdi, rdx
    call    strlen_
    mov     ebx, eax
    test    ebx, ebx
    jz      .out
    mov     edi, [font_base]
    sub     edi, 32
    call    glListBase
    mov     edi, 0xFF000000
    call    color
    lea     edi, [r12+1]
    lea     esi, [r13+1]
    call    glRasterPos2i
    mov     edi, ebx
    mov     esi, GL_UNSIGNED_BYTE
    mov     rdx, r14
    call    glCallLists
    mov     edi, 0xFFFFFFFF
    call    color
    mov     edi, r12d
    mov     esi, r13d
    call    glRasterPos2i
    mov     edi, ebx
    mov     esi, GL_UNSIGNED_BYTE
    mov     rdx, r14
    call    glCallLists
.out:
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
    pop     rbp
    ret

text_center:                        ; esi y, rdx string
    push    rbp
    mov     rbp, rsp
    push    rbx
    push    r12
    mov     ebx, esi
    mov     r12, rdx
    mov     rdi, rdx
    call    strlen_
    imul    eax, [font_w]
    mov     edi, [win_w]
    sub     edi, eax
    sar     edi, 1
    mov     esi, ebx
    mov     rdx, r12
    call    text
    pop     r12
    pop     rbx
    pop     rbp
    ret

sb_reset:
    mov     dword [sb_len], 0
    mov     byte [sbuf], 0
    ret

sb_str:
    lea     rdx, [sbuf]
    mov     eax, [sb_len]
.l:
    mov     cl, [rdi]
    test    cl, cl
    jz      .e
    cmp     eax, 250
    jae     .e
    mov     [rdx+rax], cl
    inc     eax
    inc     rdi
    jmp     .l
.e:
    mov     byte [rdx+rax], 0
    mov     [sb_len], eax
    ret

sb_int:
    lea     rsi, [numtmp+15]
    mov     byte [rsi], 0
    xor     r8d, r8d
    mov     eax, edi
    test    eax, eax
    jns     .d
    neg     eax
    mov     r8d, 1
.d:
    xor     edx, edx
    mov     ecx, 10
    div     ecx
    add     dl, '0'
    dec     rsi
    mov     [rsi], dl
    test    eax, eax
    jnz     .d
    test    r8d, r8d
    jz      .s
    dec     rsi
    mov     byte [rsi], '-'
.s:
    mov     rdi, rsi
    jmp     sb_str

; tile_uv: eax tile -> xmm6 tu, xmm7 tv
tile_uv:
    mov     edx, eax
    and     edx, 3
    cvtsi2ss xmm6, edx
    mulss   xmm6, [f_quarter]
    shr     eax, 2
    cvtsi2ss xmm7, eax
    mulss   xmm7, [f_quarter]
    ret

; draw_icon: edi cx, esi cy, edx block id, ecx unit (cube half-width = 2*unit)
draw_icon:
    push    rbp
    mov     rbp, rsp
    push    rbx
    push    r12
    push    r13
    push    r14
    push    r15
    sub     rsp, 24
    test    edx, edx
    jz      .out
    mov     r12d, edi
    mov     r13d, esi
    mov     r14d, ecx
    mov     [rsp], edx
    mov     edi, GL_TEXTURE_2D
    call    glEnable
    mov     edi, GL_QUADS
    call    glBegin
    xor     r15d, r15d              ; face 0 top, 1 left, 2 right
.face:
    lea     rax, [icon_col]
    mov     edi, [rax+r15*4]
    call    color
    mov     eax, [rsp]
    lea     eax, [rax+rax*2]
    xor     edx, edx
    test    r15d, r15d
    setnz   dl                      ; 0 top tile, 1 side tile
    add     eax, edx
    lea     rcx, [block_tiles]
    movzx   eax, byte [rcx+rax]
    call    tile_uv
    movss   [rsp+8], xmm6
    movss   [rsp+12], xmm7
    xor     ebx, ebx
.pt:
    lea     rax, [icon_uv]
    movss   xmm0, [f_uvlo]
    cmp     byte [rax+rbx*2], 0
    je      .iu
    movss   xmm0, [f_uvhi]
.iu:
    addss   xmm0, [rsp+8]
    movss   xmm1, [f_uvlo]
    cmp     byte [rax+rbx*2+1], 0
    je      .iv
    movss   xmm1, [f_uvhi]
.iv:
    addss   xmm1, [rsp+12]
    call    glTexCoord2f
    lea     rax, [icon_pts]
    lea     ecx, [r15*4+rbx]
    movsx   edi, byte [rax+rcx*2]
    movsx   esi, byte [rax+rcx*2+1]
    imul    edi, r14d
    add     edi, r12d
    imul    esi, r14d
    add     esi, r13d
    call    glVertex2i
    inc     ebx
    cmp     ebx, 4
    jb      .pt
    inc     r15d
    cmp     r15d, 3
    jb      .face
    call    glEnd
    mov     edi, GL_TEXTURE_2D
    call    glDisable
.out:
    add     rsp, 24
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
    pop     rbp
    ret

; draw_stack: edi x, esi y (slot top-left), edx id, ecx count
draw_stack:
    push    rbp
    mov     rbp, rsp
    push    rbx
    push    r12
    push    r13
    push    r14
    test    ecx, ecx
    jz      .out
    mov     r12d, edi
    mov     r13d, esi
    mov     r14d, ecx
    add     edi, SLOT/2
    add     esi, SLOT/2
    mov     ecx, 7
    call    draw_icon
    cmp     r14d, 1
    jbe     .out
    call    sb_reset
    mov     edi, r14d
    call    sb_int
    mov     eax, [sb_len]
    imul    eax, [font_w]
    lea     edi, [r12+SLOT-1]
    sub     edi, eax
    lea     esi, [r13+SLOT-2]
    lea     rdx, [sbuf]
    call    text
.out:
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
    pop     rbp
    ret

; =====================================================================
;  rendering
; =====================================================================
draw_highlight:
    push    rbp
    mov     rbp, rsp
    push    rbx
    push    r12
    cmp     dword [hit_flag], 0
    je      .out
    mov     edi, 0xFF000000
    call    color
    movss   xmm0, [f_two]
    call    glLineWidth
    mov     edi, GL_LINES
    call    glBegin
    xor     ebx, ebx
.e:
    lea     rax, [edges]
    movzx   r12d, byte [rax+rbx]
    movss   xmm3, [f_hl_lo]
    test    r12d, 1
    jz      .a
    movss   xmm3, [f_hl_hi]
.a:
    cvtsi2ss xmm0, dword [hit]
    addss   xmm0, xmm3
    movss   xmm3, [f_hl_lo]
    test    r12d, 2
    jz      .b
    movss   xmm3, [f_hl_hi]
.b:
    cvtsi2ss xmm1, dword [hit+4]
    addss   xmm1, xmm3
    movss   xmm3, [f_hl_lo]
    test    r12d, 4
    jz      .c
    movss   xmm3, [f_hl_hi]
.c:
    cvtsi2ss xmm2, dword [hit+8]
    addss   xmm2, xmm3
    call    glVertex3f
    inc     ebx
    cmp     ebx, 24
    jb      .e
    call    glEnd
.out:
    pop     r12
    pop     rbx
    pop     rbp
    ret

draw_hotbar:
    push    rbp
    mov     rbp, rsp
    push    rbx
    push    r12
    push    r13
    push    r14
    push    r15
    sub     rsp, 8
    mov     r14d, [win_w]
    sub     r14d, 9*SSTEP-4
    sar     r14d, 1                 ; x0
    mov     r15d, [win_h]
    sub     r15d, SLOT+12           ; y0
    xor     ebx, ebx
.slot:
    imul    r12d, ebx, SSTEP
    add     r12d, r14d
    cmp     ebx, [sel]
    jne     .bg
    mov     edi, 0xFFFFFFFF
    call    color
    lea     edi, [r12-3]
    lea     esi, [r15-3]
    mov     edx, SLOT+6
    mov     ecx, SLOT+6
    call    rect
.bg:
    mov     edi, 0xA0000000
    call    color
    mov     edi, r12d
    mov     esi, r15d
    mov     edx, SLOT
    mov     ecx, SLOT
    call    rect
    lea     rax, [inv_id]
    movzx   edx, byte [rax+rbx]
    lea     rax, [inv_cnt]
    movzx   ecx, byte [rax+rbx]
    mov     edi, r12d
    mov     esi, r15d
    call    draw_stack
    inc     ebx
    cmp     ebx, 9
    jb      .slot
    ; name of the selected item
    mov     eax, [sel]
    lea     rcx, [inv_id]
    movzx   eax, byte [rcx+rax]
    lea     rcx, [block_names]
    mov     rdx, [rcx+rax*8]
    lea     esi, [r15-10]
    call    text_center
    lea     esi, [r15-30]
    lea     rdx, [s_help]
    call    text_center
    add     rsp, 8
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
    pop     rbp
    ret

draw_inventory:
    push    rbp
    mov     rbp, rsp
    push    rbx
    push    r12
    push    r13
    push    r14
    push    r15
    sub     rsp, 8
    mov     edi, 0x90000000         ; dim the world
    call    color
    xor     edi, edi
    xor     esi, esi
    mov     edx, [win_w]
    mov     ecx, [win_h]
    call    rect
    call    inv_layout
    mov     r14d, r8d
    mov     r15d, r9d
    mov     edi, 0xF0383838         ; panel
    call    color
    lea     edi, [r14-12]
    lea     esi, [r15-30]
    mov     edx, 9*SSTEP+20
    mov     ecx, 30+4*SSTEP+14+14
    call    rect
    mov     edi, 0xFF5A5A5A         ; separator above the hotbar row
    call    color
    mov     edi, r14d
    lea     esi, [r15+3*SSTEP+4]
    mov     edx, 9*SSTEP-4
    mov     ecx, 2
    call    rect
    mov     edi, r14d
    lea     esi, [r15-10]
    lea     rdx, [s_invtitle]
    call    text
    call    slot_at_mouse
    mov     r13d, eax               ; hovered slot
    xor     ebx, ebx
.slot:
    mov     edi, ebx
    call    slot_pos
    mov     r12d, eax
    mov     [rsp], edx
    mov     edi, 0xFF1E1E1E
    cmp     ebx, r13d
    jne     .sc
    mov     edi, 0xFF707070
.sc:
    call    color
    mov     edi, r12d
    mov     esi, [rsp]
    mov     edx, SLOT
    mov     ecx, SLOT
    call    rect
    lea     rax, [inv_id]
    movzx   edx, byte [rax+rbx]
    lea     rax, [inv_cnt]
    movzx   ecx, byte [rax+rbx]
    mov     edi, r12d
    mov     esi, [rsp]
    call    draw_stack
    inc     ebx
    cmp     ebx, NSLOTS
    jb      .slot
    ; help line under the panel
    lea     esi, [r15+4*SSTEP+40]
    lea     rdx, [s_invhelp]
    call    text_center
    ; tooltip for the hovered stack
    test    r13d, r13d
    js      .held
    cmp     byte [held_cnt], 0
    jne     .held
    lea     rax, [inv_id]
    movzx   eax, byte [rax+r13]
    test    eax, eax
    jz      .held
    lea     rcx, [block_names]
    mov     rdx, [rcx+rax*8]
    mov     edi, [mouse_x]
    add     edi, 14
    mov     esi, [mouse_y]
    sub     esi, 8
    call    text
.held:
    movzx   edx, byte [held_id]
    movzx   ecx, byte [held_cnt]
    mov     edi, [mouse_x]
    sub     edi, SLOT/2
    mov     esi, [mouse_y]
    sub     esi, SLOT/2
    call    draw_stack
    add     rsp, 8
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
    pop     rbp
    ret

draw_hud:
    push    rbp
    mov     rbp, rsp
    push    rbx
    push    r12
    push    r13
    push    r14
    push    r15
    sub     rsp, 8
    mov     edi, GL_DEPTH_TEST
    call    glDisable
    mov     edi, GL_FOG
    call    glDisable
    mov     edi, GL_BLEND
    call    glEnable
    mov     edi, GL_PROJECTION
    call    glMatrixMode
    call    glLoadIdentity
    xorpd   xmm0, xmm0
    cvtsi2sd xmm1, dword [win_w]
    cvtsi2sd xmm2, dword [win_h]
    xorpd   xmm3, xmm3
    movsd   xmm4, [d_m1]
    movsd   xmm5, [d_p1]
    call    glOrtho
    mov     edi, GL_MODELVIEW
    call    glMatrixMode
    call    glLoadIdentity
    mov     r12d, [win_w]
    shr     r12d, 1
    mov     r13d, [win_h]
    shr     r13d, 1

    cmp     byte [inv_open], 0
    je      .game
    call    draw_inventory
    jmp     .debug
.game:
    cmp     byte [locked], 0
    jne     .cross
    mov     edi, 0x82000000
    call    color
    xor     edi, edi
    xor     esi, esi
    mov     edx, [win_w]
    mov     ecx, [win_h]
    call    rect
    lea     esi, [r13-20]
    lea     rdx, [s_logo]
    call    text_center
    lea     esi, [r13+12]
    lea     rdx, [s_click]
    call    text_center
    jmp     .bar
.cross:
    mov     edi, 0xDCFFFFFF
    call    color
    lea     edi, [r12-9]
    lea     esi, [r13-1]
    mov     edx, 18
    mov     ecx, 2
    call    rect
    lea     edi, [r12-1]
    lea     esi, [r13-9]
    mov     edx, 2
    mov     ecx, 18
    call    rect
.bar:
    call    draw_hotbar
.debug:
    call    sb_reset
    lea     rdi, [s_fps]
    call    sb_str
    mov     edi, [fps_val]
    call    sb_int
    lea     rdi, [s_xyz]
    call    sb_str
    movss   xmm0, [px]
    call    ffloor
    mov     edi, eax
    call    sb_int
    lea     rdi, [s_sp]
    call    sb_str
    movss   xmm0, [py]
    call    ffloor
    mov     edi, eax
    call    sb_int
    lea     rdi, [s_sp]
    call    sb_str
    movss   xmm0, [pz]
    call    ffloor
    mov     edi, eax
    call    sb_int
    lea     rdi, [s_chunk]
    call    sb_str
    movss   xmm0, [px]
    call    ffloor
    sar     eax, 4
    mov     edi, eax
    call    sb_int
    lea     rdi, [s_comma]
    call    sb_str
    movss   xmm0, [pz]
    call    ffloor
    sar     eax, 4
    mov     edi, eax
    call    sb_int
    mov     edi, 10
    mov     esi, 20
    lea     rdx, [sbuf]
    call    text
    mov     edi, GL_BLEND
    call    glDisable
    add     rsp, 8
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
    pop     rbp
    ret

render:
    push    rbp
    mov     rbp, rsp
    push    rbx
    push    r12
    movss   xmm0, [sky]
    movss   xmm1, [sky+4]
    movss   xmm2, [sky+8]
    movss   xmm3, [sky+12]
    call    glClearColor
    mov     edi, 0x4100
    call    glClear
    mov     edi, GL_DEPTH_TEST
    call    glEnable
    mov     edi, GL_FOG
    call    glEnable
    mov     edi, GL_PROJECTION
    call    glMatrixMode
    call    glLoadIdentity
    cvtsi2sd xmm1, dword [win_w]
    cvtsi2sd xmm2, dword [win_h]
    divsd   xmm1, xmm2
    mulsd   xmm1, [d_ftop]
    movsd   xmm0, xmm1
    xorpd   xmm0, [dsignmask]
    movsd   xmm3, [d_ftop]
    movsd   xmm2, xmm3
    xorpd   xmm2, [dsignmask]
    movsd   xmm4, [d_near]
    movsd   xmm5, [d_far]
    call    glFrustum
    mov     edi, GL_MODELVIEW
    call    glMatrixMode
    call    glLoadIdentity
    movss   xmm0, [pitch]
    mulss   xmm0, [f_rad2deg]
    xorps   xmm0, [signmask4]
    movss   xmm1, [f_one]
    xorps   xmm2, xmm2
    xorps   xmm3, xmm3
    call    glRotatef
    movss   xmm0, [yaw]
    mulss   xmm0, [f_rad2deg]
    xorps   xmm0, [signmask4]
    xorps   xmm1, xmm1
    movss   xmm2, [f_one]
    xorps   xmm3, xmm3
    call    glRotatef
    movss   xmm0, [px]
    xorps   xmm0, [signmask4]
    movss   xmm1, [py]
    addss   xmm1, [f_eye]
    xorps   xmm1, [signmask4]
    movss   xmm2, [pz]
    xorps   xmm2, [signmask4]
    call    glTranslatef

    mov     edi, GL_TEXTURE_2D
    call    glEnable
    xor     ebx, ebx
.op:
    mov     edi, ebx
    shl     edi, 1
    add     edi, [list_base]
    call    glCallList
    inc     ebx
    cmp     ebx, NCHUNKS
    jb      .op
    mov     edi, GL_TEXTURE_2D
    call    glDisable
    call    draw_highlight

    mov     edi, GL_TEXTURE_2D
    call    glEnable
    mov     edi, GL_BLEND
    call    glEnable
    xor     edi, edi
    call    glDepthMask
    xor     ebx, ebx
.gl:
    mov     edi, ebx
    shl     edi, 1
    add     edi, [list_base]
    inc     edi
    call    glCallList
    inc     ebx
    cmp     ebx, NCHUNKS
    jb      .gl
    mov     edi, 1
    call    glDepthMask
    mov     edi, GL_BLEND
    call    glDisable
    mov     edi, GL_TEXTURE_2D
    call    glDisable

    call    draw_hud
    pop     r12
    pop     rbx
    pop     rbp
    ret

now_ns:
    mov     eax, 228
    mov     edi, 1
    lea     rsi, [ts]
    syscall
    mov     rax, [ts]
    imul    rax, rax, 1000000000
    add     rax, [ts+8]
    ret

; =====================================================================
;  main
; =====================================================================
main:
    push    rbp
    mov     rbp, rsp
    push    rbx
    push    r12

    xor     edi, edi
    call    XOpenDisplay
    test    rax, rax
    jnz     .disp_ok
    lea     rdi, [s_nodisp]
    call    puts
    mov     eax, 1
    jmp     .ret
.disp_ok:
    mov     [dpy], rax
    mov     rdi, rax
    call    XDefaultScreen
    mov     [screen], eax
    mov     rdi, [dpy]
    mov     esi, eax
    call    XRootWindow
    mov     [root], rax
    mov     rdi, [dpy]
    mov     esi, [screen]
    lea     rdx, [vis_attr]
    call    glXChooseVisual
    test    rax, rax
    jnz     .vis_ok
    lea     rdi, [s_novis]
    call    puts
    mov     eax, 1
    jmp     .ret
.vis_ok:
    mov     [vinfo], rax
    mov     rdi, [dpy]
    mov     rsi, [root]
    mov     rax, [vinfo]
    mov     rdx, [rax]
    xor     ecx, ecx
    call    XCreateColormap
    mov     [swa+96], rax
    mov     qword [swa+24], 0
    mov     qword [swa+72], 0x22804F
    lea     rax, [swa]
    push    rax
    push    0x2808
    mov     rax, [vinfo]
    push    qword [rax]
    push    1
    mov     eax, [rax+20]
    push    rax
    push    0
    mov     rdi, [dpy]
    mov     rsi, [root]
    xor     edx, edx
    xor     ecx, ecx
    mov     r8d, WIN_W0
    mov     r9d, WIN_H0
    call    XCreateWindow
    add     rsp, 48
    mov     [win], rax
    mov     rdi, [dpy]
    mov     rsi, [win]
    lea     rdx, [s_title]
    call    XStoreName
    mov     rdi, [dpy]
    lea     rsi, [s_wmdel]
    xor     edx, edx
    call    XInternAtom
    mov     [wm_delete], rax
    mov     rdi, [dpy]
    mov     rsi, [win]
    lea     rdx, [wm_delete]
    mov     ecx, 1
    call    XSetWMProtocols
    mov     rdi, [dpy]
    mov     rsi, [win]
    call    XMapWindow

    mov     rdi, [dpy]
    mov     rsi, [vinfo]
    xor     edx, edx
    mov     ecx, 1
    call    glXCreateContext
    mov     [glctx], rax
    mov     rdi, [dpy]
    mov     rsi, [win]
    mov     rdx, [glctx]
    call    glXMakeCurrent
    mov     rdi, [dpy]
    mov     esi, 1
    xor     edx, edx
    call    XkbSetDetectableAutoRepeat

    mov     rdi, [dpy]
    mov     rsi, [win]
    lea     rdx, [blank_bits]
    mov     ecx, 8
    mov     r8d, 8
    call    XCreateBitmapFromData
    mov     rbx, rax
    sub     rsp, 8
    push    0
    mov     rdi, [dpy]
    mov     rsi, rbx
    mov     rdx, rbx
    lea     rcx, [black_col]
    lea     r8, [black_col]
    xor     r9d, r9d
    call    XCreatePixmapCursor
    add     rsp, 16
    mov     [blank_cursor], rax

    mov     rdi, [dpy]
    lea     rsi, [s_font1]
    call    XLoadQueryFont
    test    rax, rax
    jnz     .font_ok
    mov     rdi, [dpy]
    lea     rsi, [s_font2]
    call    XLoadQueryFont
    test    rax, rax
    jz      .nofont
.font_ok:
    mov     rbx, rax
    movsx   eax, word [rbx+72]
    mov     [font_w], eax
    mov     edi, 96
    call    glGenLists
    mov     [font_base], eax
    mov     rdi, [rbx+8]
    mov     esi, 32
    mov     edx, 96
    mov     ecx, [font_base]
    call    glXUseXFont
.nofont:

    mov     edi, GL_CULL_FACE
    call    glDisable
    mov     edi, GL_FOG_MODE
    mov     esi, GL_LINEAR
    call    glFogi
    mov     edi, GL_FOG_START
    movss   xmm0, [f_fogs]
    call    glFogf
    mov     edi, GL_FOG_END
    movss   xmm0, [f_foge]
    call    glFogf
    mov     edi, GL_FOG_COLOR
    lea     rsi, [sky]
    call    glFogfv
    mov     edi, GL_SRC_ALPHA
    mov     esi, GL_ONE_MINUS_SRC_ALPHA
    call    glBlendFunc
    xor     edi, edi
    xor     esi, esi
    mov     edx, WIN_W0
    mov     ecx, WIN_H0
    call    glViewport

    call    load_textures
    call    gen_world
    mov     edi, NCHUNKS*2
    call    glGenLists
    mov     [list_base], eax
    xor     ebx, ebx
.bc:
    mov     edi, ebx
    call    build_chunk
    inc     ebx
    cmp     ebx, NCHUNKS
    jb      .bc
    call    spawn

    call    now_ns
    mov     [t_last], rax
.loop:
    call    pump_events
    cmp     byte [running], 0
    je      .quit
    call    now_ns
    mov     r12, rax
    mov     rbx, rax
    sub     rbx, [t_last]
    mov     [t_last], rax
    add     [fps_acc], rbx
    inc     dword [fps_frames]
    cmp     qword [fps_acc], 1000000000
    jb      .nofps
    mov     eax, [fps_frames]
    mov     [fps_val], eax
    mov     dword [fps_frames], 0
    mov     qword [fps_acc], 0
.nofps:
    cvtsi2sd xmm0, rbx
    mulsd   xmm0, [d_nsec]
    cvtsd2ss xmm0, xmm0
    minss   xmm0, [f_dtmax]
    call    update_player
    call    raycast
    call    render
    mov     rdi, [dpy]
    mov     rsi, [win]
    call    glXSwapBuffers
    call    now_ns
    sub     rax, r12
    mov     rcx, 4000000
    sub     rcx, rax
    jle     .loop
    mov     qword [sleep_ts], 0
    mov     [sleep_ts+8], rcx
    mov     eax, 35
    lea     rdi, [sleep_ts]
    xor     esi, esi
    syscall
    jmp     .loop

.quit:
    call    unlock_mouse
    mov     rdi, [dpy]
    xor     esi, esi
    xor     edx, edx
    call    glXMakeCurrent
    mov     rdi, [dpy]
    mov     rsi, [glctx]
    call    glXDestroyContext
    mov     rdi, [dpy]
    call    XCloseDisplay
    xor     eax, eax
.ret:
    pop     r12
    pop     rbx
    pop     rbp
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
