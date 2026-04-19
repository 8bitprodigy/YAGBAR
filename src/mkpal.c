#include <stdio.h>
#include <stdint.h>

#define RGB15(r,g,b) (((r)&31) | (((g)&31)<<5) | (((b)&31)<<10))
typedef uint8_t u8;


// ... your hues table and palette loop here ...
// Instead of writing to pal_bg_mem, collect into:
uint16_t pal_bg_mem[256];


// ---------------------------------------------------------------------------
// Palette setup
// ---------------------------------------------------------------------------
static 
void 
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
        {  9, 20,  8 },  // row 7:  green
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

int main(void) {
    setupPalette();
    // then dump:
    printf("GIMP Palette\nName: GBAraycaster\nColumns: 16\n#\n");
    for (int i = 0; i < 256; i++) {
        int r = (pal_bg_mem[i]      & 31) << 3;
        int g = (pal_bg_mem[i] >> 5 & 31) << 3;
        int b = (pal_bg_mem[i] >>10 & 31) << 3;
        printf("%3d %3d %3d\tIndex %d\n", r, g, b, i);
    }
}
