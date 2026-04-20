#ifndef DEFS_H
#define DEFS_H


/*******************************************************************************
	CORE CONSTANTS / FLAGS
*******************************************************************************/
#ifndef YAGBAR_MAX_ENTITIES
	#define YAGBAR_MAX_ENTITIES 256
#endif

#ifndef YAGBAR_BITS_PRECISION
	#define YAGBAR_BITS_PRECISION (10)
#endif
/*  Number of YAGBAR_Units in a side of a
	spatial square, i.e. the fixed point
	scaling. */
#define YAGBAR_UNITS_PER_SQUARE (1 << YAGBAR_BITS_PRECISION) 

#define YAGBAR_INFINITY 2000000000
#define YAGBAR_U        YAGBAR_UNITS_PER_SQUARE ///< shorthand for YAGBAR_UNITS_PER_SQUARE


/*******************************************************************************
	RENDER CONSTANTS / FLAGS
*******************************************************************************/
#define RENDER_PIXEL_FUNCTION             pixelFunc
//#define RENDER_COLUMN_FUNCTION            flatColumnFunc
#define RENDER_RS_HEIGHT_FN               YAGBAR_heightAt
#define RENDER_COMPUTE_FLOOR_DEPTH        0
#define RENDER_COMPUTE_CEILING_DEPTH      0
#define RENDER_COMPUTE_FLOOR_TEXCOORDS    0
#define RENDER_UNIT_DIVISOR_NUM_SHIFTS   (0)
#define RENDER_UNITS_PER_SQUARE          (1024 >> RENDER_UNIT_DIVISOR_NUM_SHIFTS)
#define RENDER_HORIZONTAL_FOV            (RENDER_UNITS_PER_SQUARE / 4)
#define RENDER_VERTICAL_FOV              (RENDER_UNITS_PER_SQUARE / 3)
#define RENDER_NUM_VISIBLE_SPRITES       128
#define RENDER_PAL_TRANSPARENT           255


/*******************************************************************************
	MATH CONSTANTS / FLAGS
*******************************************************************************/
#define MATH_USE_DIST_APPROX            2
#define MATH_USE_COS_LUT                2
#define DEPTH_RECIPROCAL_SIZE        1024


#define DEBUG_PROFILE        0
#define WALLS_TEXTURED       1
#define DEPTH_SHADE_WALLS    0
#define DEPTH_SHADE_FLOOR    0
#define DEPTH_SHADE_CEILING  0
#define DEPTH_SHIFT_AMOUNT  11
/*  Shade walls depending on their cardinal direction:
		0 - Off
		1 - Indexed
		2 - Wolf3D-like
*/
#define USE_SIDE_SHADING     1
#define TEXTURED_FLOOR       0
#define TEXTURED_CEILING     0
#define COLORED_FLOOR        0
 
/*******************************************************************************
	Screen / GBA constants
*******************************************************************************/
#define RENDER_W      120
#define SCREEN_W      240
#define SCREEN_H      160
#define CAMERA_HEIGHT ((YAGBAR_UNITS_PER_SQUARE >> 3) * 10)
#define TURN_SPEED    (16 >> RENDER_UNIT_DIVISOR_NUM_SHIFTS)   // RENDER angle units per frame
#define MOVE_SPEED    (192 >> RENDER_UNIT_DIVISOR_NUM_SHIFTS)   // RENDER sub-units per frame  (~0.3 squares)

 
/*  palette index -- color map (we fill pal_bg_mem[] at startup) */
#define PAL_BLACK   0
#define PAL_WALL   10
#define PAL_FLOOR 163
#define PAL_CEIL    5
#define PAL_SKY   164

#define LEVEL_W 20
#define LEVEL_H 15


#endif /* DEFS_H */
