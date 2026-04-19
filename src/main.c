// GBA Raycaster Demo using raycastlib + libtonc
// Mode 4 (8BPP, 240x160), double-buffered.
//
// Build example (devkitARM):
/*
  arm-none-eabi-gcc -mthumb -mthumb-interwork -march=armv4t \
    -O2 -Wall -Wextra \
    -I$(DEVKITPRO)/libtonc/include \
    -L$(DEVKITPRO)/libtonc/lib \
    -specs=gba.specs \
    gba_raycaster.c -o raycaster.elf -ltonc
  arm-none-eabi-objcopy -O binary raycaster.elf raycaster.gba
*/
// raycastlib.h must be in the same directory (or on the include path).
// Get it from: https://github.com/drummyfish/raycastlib

#include <tonc.h>

#include "yagbar.h"

// mGBA debug register
/*
#define REG_DEBUG_STRING ((volatile char*)0x04FFF600)
#define REG_DEBUG_FLAGS  ((volatile u16*)0x04FFF700)
#define DEBUG_ENABLE     (0x0100)
//*/
// ---------------------------------------------------------------------------
// raycastlib config  (must come BEFORE the #include)
// ---------------------------------------------------------------------------

#define RCL_PIXEL_FUNCTION             pixelFunc
//#define RCL_COLUMN_FUNCTION            flatColumnFunc
#define RCL_RS_HEIGHT_FN               heightAt
#define RCL_USE_DIST_APPROX            2
#define RCL_COMPUTE_FLOOR_DEPTH        1
#define RCL_COMPUTE_CEILING_DEPTH      1
#define RCL_COMPUTE_FLOOR_TEXCOORDS    0
#define RCL_USE_COS_LUT                2
#define RCL_UNIT_DIVISOR_NUM_SHIFTS   (0)
#define RCL_UNITS_PER_SQUARE          (1024 >> RCL_UNIT_DIVISOR_NUM_SHIFTS)
#define RCL_HORIZONTAL_FOV            (RCL_UNITS_PER_SQUARE / 4)
#define RCL_VERTICAL_FOV              (RCL_UNITS_PER_SQUARE / 3)

#define RAYCASTLIB_GBA_IMPLEMENTATION



#define DEBUG_PROFILE        1
#define WALLS_TEXTURED       1
#define DEPTH_SHADE_WALLS    1
#define DEPTH_SHADE_FLOOR    1
#define DEPTH_SHADE_CEILING  1
#define DEPTH_SHIFT_AMOUNT  11
#define USE_SIDE_SHADING     1
#define TEXTURED_FLOOR       0
#define TEXTURED_CEILING     0
#define COLORED_FLOOR        0
 
// ---------------------------------------------------------------------------
// Screen / GBA constants
// ---------------------------------------------------------------------------
#define RENDER_W      120
#define SCREEN_W      240
#define SCREEN_H      160
#define CAMERA_HEIGHT ((RCL_UNITS_PER_SQUARE >> 3) * 9)
#define TURN_SPEED    (16 >> RCL_UNIT_DIVISOR_NUM_SHIFTS)   // RCL angle units per frame
#define MOVE_SPEED    (192 >> RCL_UNIT_DIVISOR_NUM_SHIFTS)   // RCL sub-units per frame  (~0.3 squares)

 
// Mode 4 VRAM is 8-bit pixels but the bus is 16-bit wide, so we can only
// write 16-bit (halfword) aligned pairs.  Helper: write a single palette
// index to an (x, y) position safely.
//
// The back-buffer page starts at 0x0600A000 when using page-flipping
// (vid_flip / VID_PAGE_B).  Tonc exposes m4_mem / m4_mem_back for each page.
 
// palette index → colour map (we fill pal_bg_mem[] at startup)
#define PAL_BLACK   0
#define PAL_WALL   10
#define PAL_FLOOR 163
#define PAL_CEIL    5
#define PAL_SKY   164


//#include "raycastlib_gba.h"
 
// ---------------------------------------------------------------------------
// Level map
// ---------------------------------------------------------------------------
#define LEVEL_W 20
#define LEVEL_H 15

 
u8 my_color = 0;



// ---------------------------------------------------------------------------
// raycastlib callbacks
// ---------------------------------------------------------------------------
 
// Pointer to the buffer we are currently drawing into (set each frame).
/*
static u16 *drawBuf;
*/
static u8   show_palette = 0;
static u8   use_textures = 0;
static u16 *drawBuf;

 
// Write a single 8-bit palette index into Mode 4 VRAM.
// Mode 4 VRAM is halfword-addressed: each u16 holds two side-by-side pixels.
//   bits 0-7  → left pixel  (even x)
//   bits 8-15 → right pixel (odd  x)
//
// We do a read-modify-write so we never corrupt the neighbouring pixel.

IWRAM_CODE 
static inline 
void 
m4__plot(int x, int y, u8 clr)
{
    // Address of the halfword that contains pixel (x, y)
    u16 *p = drawBuf + ((y * SCREEN_W + x) >> 1);
 
    if (x & 1)
        // odd column → high byte
        *p = (*p & 0x00FF) | ((u16)clr << 8);
    else
        // even column → low byte
        *p = (*p & 0xFF00) | clr;
}

IWRAM_CODE
static inline 
u8
sampleTexture(
  const unsigned char *tex,
  YAGBAR_Unit             tx, 
  YAGBAR_Unit             ty
)
{
    const u8 w = 64;
    const u8 h = 64;
    tx = MATH_wrap(tx, RCL_UNITS_PER_SQUARE);
    ty = MATH_wrap(ty, RCL_UNITS_PER_SQUARE);
    register s16 px = (w * tx) >> 10;
    register s16 py = (h * ty) >> 10;
    return tex[2 + h * px + py];
}

// Write a vertical strip of a single color in a column
// col: screen x (already doubled), yStart/yEnd: pixel rows
IWRAM_CODE 
static inline
void 
flatColumnFunc(
    YAGBAR_Unit x, 
    YAGBAR_Unit yStart, 
    YAGBAR_Unit yEnd, 
    u8       isFloor
)
{
    register u16 word = isFloor ? 
        (PAL_FLOOR | (PAL_FLOOR << 8)) : 
        (PAL_CEIL  | (PAL_CEIL  << 8));
    register u16 *ptr = drawBuf + yStart * (RENDER_W) + x;
    register u8 count = yEnd - yStart;
    register u8 n     = (count + 15) >> 4;
    //for (int y = yStart; y <= yEnd; y++)
    switch (count% 16) {
    case 0: do { *ptr = word; ptr += RENDER_W;
    case 15:     *ptr = word; ptr += RENDER_W;
    case 14:     *ptr = word; ptr += RENDER_W;
    case 13:     *ptr = word; ptr += RENDER_W;
    case 12:     *ptr = word; ptr += RENDER_W;
    case 11:     *ptr = word; ptr += RENDER_W;
    case 10:     *ptr = word; ptr += RENDER_W;
    case  9:     *ptr = word; ptr += RENDER_W;
    case  8:     *ptr = word; ptr += RENDER_W;
    case  7:     *ptr = word; ptr += RENDER_W;
    case  6:     *ptr = word; ptr += RENDER_W;
    case  5:     *ptr = word; ptr += RENDER_W;
    case  4:     *ptr = word; ptr += RENDER_W;
    case  3:     *ptr = word; ptr += RENDER_W;
    case  2:     *ptr = word; ptr += RENDER_W;
    case  1:     *ptr = word; ptr += RENDER_W;
            } while (--n > 0);
    }
}

 
// ---------------------------------------------------------------------------
// Palette setup
// ---------------------------------------------------------------------------
static inline u8 
rgbToIndex(u8 r, u8 g, u8 b)
{
  return (r & 3) | ((g & 7) << 2) | ((b & 7) << 5);
}

static void 
setupPalette(void)
{
    // Each row: {r, g, b} at full saturation midpoint
    // Ramp goes dark desaturated -> full colour -> light desaturated
    static const u8 hues[16][3] = {
        { 31, 31, 31 },  // row 0:  greyscale
        { 20, 20, 12 },  // row 1:  warm grey
        { 20, 10,  0 },  // row 2:  brown
        { 31,  0,  0 },  // row 3:  red
        { 31, 12,  0 },  // row 4:  orange
        { 31, 31,  0 },  // row 5:  yellow
        { 16, 31,  0 },  // row 6:  yellow-green
        {  0, 24,  0 },  // row 7:  green
        {  0, 31, 16 },  // row 8:  teal
        {  0, 31, 31 },  // row 9:  cyan
        {  0, 16, 31 },  // row 10: sky blue
        {  0,  0, 31 },  // row 11: blue
        { 12,  0, 31 },  // row 12: indigo
        { 31,  0, 31 },  // row 13: magenta
        { 31,  0, 16 },  // row 14: rose
        { 31, 16, 16 },  // row 15: pink
    };

    for (int row = 0; row < 16; row++) {
        for (int shade = 0; shade < 16; shade++) {
            int r, g, b;

            if (row == 0) {
                int v = shade * 2;
                pal_bg_mem[shade] = RGB15(v, v, v);
                continue;
            }

            if (shade < 8) {
                // Dark tint at shade 0 -> full colour at shade 7
                int t = shade + 1;  // 2..9
                r = hues[row][0] * t / 8;
                g = hues[row][1] * t / 8;
                b = hues[row][2] * t / 8;
            }
            else {
                // Full colour -> light pastel, add a little white
                int t = shade - 7;  // 1..8
                r = hues[row][0] + (31 - hues[row][0]) * t / 9;
                g = hues[row][1] + (31 - hues[row][1]) * t / 9;
                b = hues[row][2] + (31 - hues[row][2]) * t / 9;
            }

            pal_bg_mem[row * 16 + shade] = RGB15(r, g, b);
        }
    }
}

static void 
drawPalette(void)
{
    // Draw the 16x16 palette as 15x10 pixel blocks (fills 240x160 exactly)
    for (int i = 0; i < 256; i++) {
        int px = (i % 16) * 15;  // 16 columns * 15px = 240
        int py = (i / 16) * 10;  // 16 rows    * 10px = 160

        for (int y = py; y < py + 10; y++)
            for (int x = px; x < px + 15; x++)
                m4__plot(x, y, (u8)i);
    }
}
 
// ---------------------------------------------------------------------------
// Input helpers
// ---------------------------------------------------------------------------
static void 
handleInput(YAGBAR_Camera *cam)
{
    key_poll();
 
    YAGBAR_Unit dx = 0, dy = 0;
 
    // Strafe (shoulder buttons)
    if (key_is_down(KEY_LEFT)) {
        dy =  MATH_cos(cam->angle) / (RCL_UNITS_PER_SQUARE / MOVE_SPEED);
        dx =  MATH_sin(cam->angle) / (RCL_UNITS_PER_SQUARE / MOVE_SPEED);
    }
    if (key_is_down(KEY_RIGHT)) {
        // Strafe right: rotate movement vector 90 degrees
        dy = -MATH_cos(cam->angle) / (RCL_UNITS_PER_SQUARE / MOVE_SPEED);
        dx = -MATH_sin(cam->angle) / (RCL_UNITS_PER_SQUARE / MOVE_SPEED);
    }
 
    // Forward / backward
    if (key_is_down(KEY_DOWN)) {
        dy =  MATH_sin(cam->angle) / (RCL_UNITS_PER_SQUARE / MOVE_SPEED);
        dx = -MATH_cos(cam->angle) / (RCL_UNITS_PER_SQUARE / MOVE_SPEED);
    }
    if (key_is_down(KEY_UP)) {
        dy = -MATH_sin(cam->angle) / (RCL_UNITS_PER_SQUARE / MOVE_SPEED);
        dx =  MATH_cos(cam->angle) / (RCL_UNITS_PER_SQUARE / MOVE_SPEED);
    }
 
    // Turn
    if (key_is_down(KEY_B)) cam->angle -= TURN_SPEED;
    if (key_is_down(KEY_A)) cam->angle += TURN_SPEED;

    if (key_is_down(KEY_L)) my_color--;
    if (key_is_down(KEY_R)) my_color++;

    if (key_hit(KEY_SELECT)) show_palette = !show_palette;
    if (key_hit(KEY_START))  use_textures = !use_textures;
 
    // Collision check: only move if destination square is open
    if (dx || dy)
    {
        int nx = MATH_divRoundDown(cam->position.x + dx, RCL_UNITS_PER_SQUARE);
        int ny = MATH_divRoundDown(cam->position.y + dy, RCL_UNITS_PER_SQUARE);
        int cx = MATH_divRoundDown(cam->position.x,       RCL_UNITS_PER_SQUARE);
        int cy = MATH_divRoundDown(cam->position.y,       RCL_UNITS_PER_SQUARE);
 
        // Allow sliding: try combined move first, then each axis separately
        if (YAGBAR_heightAt(nx, ny) == 0) {
            cam->position.x += dx;
            cam->position.y += dy;
        }
        else if (YAGBAR_heightAt(nx, cy) == 0) {
            cam->position.x += dx;
        }
        else if (YAGBAR_heightAt(cx, ny) == 0) {
            cam->position.y += dy;
        }
    }
}
 
// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int 
main(void)
{
    irq_init(NULL);
    irq_add(II_VBLANK, NULL);
    // --- Video mode 4, BG2 enabled, page-flip capable ---
    REG_DISPCNT = DCNT_MODE4 | DCNT_BG2;
    setupPalette();

#if DEBUG_PROFILE
    tte_init_bmp(
        4,
        NULL,
        NULL 
    );
    tte_init_con();
#endif /* DEBUG_PROFILE */
    
    // Camera initial state
    YAGBAR_Camera camera;
    YAGBAR_initCamera(&camera);
    camera.position.x   = 2 * RCL_UNITS_PER_SQUARE + (RCL_UNITS_PER_SQUARE >> 1);
    camera.position.y   = 2 * RCL_UNITS_PER_SQUARE + (RCL_UNITS_PER_SQUARE >> 1);
    camera.angle        = 0;
    camera.resolution.x = RENDER_W;
    camera.resolution.y = SCREEN_H;
    camera.height       = CAMERA_HEIGHT;
 
    // Ray constraints
    YAGBAR_RayConstraints rc;
    YAGBAR_initRayConstraints(&rc);
    rc.max_hits  = 1;
    rc.max_steps = 40;

    drawBuf = RENDER_getDrawBuffer();
 
    // Page-flip state: start drawing into the back buffer (page 1 = 0x0600A000)
/*
    int page = 0;
    u32 frame = 0;
*/
#if DEBUG_PROFILE
    profile_start();
#endif /* DEBUG_PROFILE */
    
    while (1)
    {/* 
        // Point drawBuf at whichever page is NOT currently displayed
        drawBuf = (page == 0) 
            ? (u16*)MEM_VRAM 
            : (u16*)(MEM_VRAM + 0xA000);

        REG_DISPCNT ^= DCNT_PAGE;

        if (page == 0)
            REG_DISPCNT = DCNT_MODE4 | DCNT_BG2 | DCNT_PAGE;   // show page 1
        else
            REG_DISPCNT = DCNT_MODE4 | DCNT_BG2;                // show page 0
        page ^= 1;

//*/
        // Bob the camera height gently
        camera.height = CAMERA_HEIGHT + (MATH_sin(RENDER_getFrame() << 3) >> 3);
        
        // Handle player input for next frame
        handleInput(&camera);
        // Render scene into drawBuf via pixelFunc callback
        
        if (show_palette) drawPalette();
        else RENDER_renderSimple(camera, YAGBAR_heightAt, 0, 0, rc);

#if DEBUG_PROFILE
        uint cycles = profile_stop();
        tte_init_bmp(4, NULL, NULL);
        tte_set_pos(0, 0);
        tte_printf("#{P:8,8}%i", cycles);
        profile_start();
#endif /* DEBUG_PROFILE */

        // Wait for VBlank, then flip pages
        RENDER_flip();
 
        //frame++;
    }
 
    return 0;  // unreachable
}
