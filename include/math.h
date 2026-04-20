#ifndef YAGBAR_MATH_H
#define YAGBAR_MATH_H


#include <tonc.h>

#include "core.h"
#include "reciprocal_table.h"


/*******************************************************************************
    CONSTANTS / FLAGS
*******************************************************************************/
#ifndef MATH_USE_COS_LUT
    /*  type of look up table for cos function:
            0: none (compute)
            1: 64 items
            2: 128 items */
    #define MATH_USE_COS_LUT 0 
#endif /* MATH_USE_COS_LUT */

#ifndef MATH_USE_DIST_APPROX
    /*  What distance approximation to use:
            0: none (compute full Euclidean distance)
            1: accurate approximation
            2: octagonal approximation (LQ) */
    #define MATH_USE_DIST_APPROX 0 
#endif /* MATH_USE_DIST_APPROX */


/*******************************************************************************
    MACROS
*******************************************************************************/
#define MATH_min(a,b)        ((a) < (b) ? (a) : (b))
#define MATH_max(a,b)        ((a) > (b) ? (a) : (b))
#define MATH_nonZero(v)      ((v) + ((v) == 0)) ///< To prevent zero divisions.
#define MATH_zeroClamp(x)    ((x) * ((x) >= 0))
#define MATH_likely(cond)    __builtin_expect(!!(cond),1) 
#define MATH_unlikely(cond)  __builtin_expect(!!(cond),0) 

// Bhaskara's cosine approximation formula
#define trigHelper(x) (((YAGBAR_Unit) YAGBAR_UNITS_PER_SQUARE) *\
    ((YAGBAR_UNITS_PER_SQUARE >> 1) * (YAGBAR_UNITS_PER_SQUARE >> 1) - 4 * (x) * (x)) /\
    ((YAGBAR_UNITS_PER_SQUARE >> 1) * (YAGBAR_UNITS_PER_SQUARE >> 1) + (x) * (x)))


#ifdef YAGBAR_DEBUG_MATH
    /* NOTE: Update with mGBA debug commands in place of `printf`
    #define MATH_logV2D(v)\
        printf("[%d,%d]\n",v.x,v.y);

    #define MATH_logRay(r){\
        printf("ray:\n");\
        printf("  start: ");\
        MATH_logV2D(r.start);\
        printf("  dir: ");\
        MATH_logV2D(r.direction);}

    #define MATH_logHitResult(h){\
        printf("hit:\n");\
        printf("  square: ");\
        MATH_logV2D(h.square);\
        printf("  pos: ");\
        MATH_logV2D(h.position);\
        printf("  dist: %d\n", h.distance);\
        printf("  dir: %d\n", h.direction);\
        printf("  texcoord: %d\n", h.textureCoord);}

    #define MATH_logPixelInfo(p){\
        printf("pixel:\n");\
        printf("  position: ");\
        MATH_logV2D(p.position);\
        printf("  texCoord: ");\
        MATH_logV2D(p.texCoords);\
        printf("  depth: %d\n", p.depth);\
        printf("  height: %d\n", p.height);\
        printf("  wall: %d\n", p.isWall);\
        printf("  hit: ");\
        MATH_logHitResult(p.hit);\
        }

    #define MATH_logCamera(c){\
        printf("camera:\n");\
        printf("  position: ");\
        MATH_logV2D(c.position);\
        printf("  height: %d\n",c.height);\
        printf("  direction: %d\n",c.direction);\
        printf("  shear: %d\n",c.shear);\
        printf("  resolution: %d x %d\n",c.resolution.x,c.resolution.y);\
        }
    */
#endif /* YAGBAR_DEBUG_MATH */

/*******************************************************************************
    GLOBALS
*******************************************************************************/
#if MATH_USE_COS_LUT != 0
extern const YAGBAR_Unit cosLUT[];
#endif /* MATH_USE_COS_LUT */


/*******************************************************************************
    PUBLIC METHODS / FUNCTIONS
*******************************************************************************/
IWRAM_CODE
static inline
YAGBAR_Unit
MATH_fast_div(YAGBAR_Unit numerator, YAGBAR_Unit denominator)
{
    if ((u32)denominator < DEPTH_RECIPROCAL_SIZE)
        return (YAGBAR_Unit)(((int64_t)numerator * depth_reciprocal[denominator]) >> 10);
    return numerator / denominator;
}

IWRAM_CODE
static inline
YAGBAR_Unit
MATH_fast_mod(YAGBAR_Unit numerator, YAGBAR_Unit modulus)
{
    if ((u32)modulus < DEPTH_RECIPROCAL_SIZE) {
        register s32 prod = MATH_fast_div(numerator, modulus);
        return numerator - ((prod >> 10) * modulus);
    }
    return numerator % modulus;
}

/// Like mod, but behaves differently for negative values.
IWRAM_CODE 
static inline 
YAGBAR_Unit 
MATH_wrap(YAGBAR_Unit value, YAGBAR_Unit mod)
{
    YAGBAR_Unit cmp = value < 0;
    return cmp * mod + MATH_fast_mod(value, mod) - cmp;
}

IWRAM_CODE 
static inline
YAGBAR_Unit 
MATH_clamp(YAGBAR_Unit value, YAGBAR_Unit valueMin, YAGBAR_Unit valueMax)
{
    if (value >= valueMin)
    {
        if (value <= valueMax)
            return value;
        else
            return valueMax;
    }
    else
        return valueMin;
}

/// Performs division, rounding down, NOT towards zero.
IWRAM_CODE 
static inline 
YAGBAR_Unit 
MATH_divRoundDown(YAGBAR_Unit value, YAGBAR_Unit divisor)
{
    return MATH_fast_div(value, divisor) - ((value >= 0) ? 0 : 1);
}

IWRAM_CODE 
static inline 
YAGBAR_Unit 
MATH_abs(YAGBAR_Unit value)
{
    return value * (((value >= 0) << 1) - 1);
}

inline 
YAGBAR_Vec2 
MATH_angleToDirection(YAGBAR_Unit angle);

/*  Cos function.

    @param  input to cos in YAGBAR_Units (YAGBAR_UNITS_PER_SQUARE = 2 * pi = 360 degrees)
    @return MATH_normalized output in YAGBAR_Units (from -YAGBAR_UNITS_PER_SQUARE to
        YAGBAR_UNITS_PER_SQUARE)
*/
IWRAM_CODE
static inline
YAGBAR_Unit 
MATH_cos(YAGBAR_Unit input)
{
    input = MATH_wrap(input,YAGBAR_UNITS_PER_SQUARE);

#if MATH_USE_COS_LUT == 1

    #ifdef RCL_RAYCAST_TINY
        return cosLUT[input];
    #else
        return cosLUT[input >> 4];
    #endif

#elif MATH_USE_COS_LUT == 2
    return cosLUT[input >> 3];
#else
    if (input < YAGBAR_UNITS_PER_SQUARE >> 2)
        return trigHelper(input);
    else if (input < YAGBAR_UNITS_PER_SQUARE >> 1)
        return -1 * trigHelper((YAGBAR_UNITS_PER_SQUARE >> 1) - input);
    else if (input < 3 * YAGBAR_UNITS_PER_SQUARE >> 2)
        return -1 * trigHelper(input - (YAGBAR_UNITS_PER_SQUARE >> 1));
    else
        return trigHelper(YAGBAR_UNITS_PER_SQUARE - input);
#endif
}

#undef trigHelper
IWRAM_CODE 
static inline
YAGBAR_Unit 
MATH_sin(YAGBAR_Unit input)
{
    return MATH_cos(input - (YAGBAR_UNITS_PER_SQUARE >> 2));
}

inline
YAGBAR_Unit 
MATH_tan(YAGBAR_Unit input);
inline
YAGBAR_Unit 
MATH_ctg(YAGBAR_Unit input);

/// Normalizes given vector to have YAGBAR_UNITS_PER_SQUARE length.
inline
YAGBAR_Vec2 
MATH_normalize(YAGBAR_Vec2 v);

/// Computes a cos of an angle between two vectors.
inline
YAGBAR_Unit 
MATH_vectorsAngleCos(YAGBAR_Vec2 v1, YAGBAR_Vec2 v2);

inline
uint16_t 
MATH_sqrt(YAGBAR_Unit value);
inline
YAGBAR_Unit 
MATH_dist(YAGBAR_Vec2 p1, YAGBAR_Vec2 p2);

IWRAM_CODE
static inline
YAGBAR_Unit 
MATH_len(YAGBAR_Vec2 v)
{
    YAGBAR_Vec2 zero;
    zero.x = 0;
    zero.y = 0;

    return MATH_dist(zero,v);
}

inline
YAGBAR_Unit 
MATH_clamp(YAGBAR_Unit value, YAGBAR_Unit valueMin, YAGBAR_Unit valueMax);

/*  Converts an angle in whole degrees to an angle in YAGBAR_Units that this 
    engine uses.
*/   
inline
YAGBAR_Unit 
MATH_degreesToUnitsAngle(int16_t degrees);

IWRAM_CODE
static inline 
int8_t 
MATH_pointIsLeftOfRay(YAGBAR_Vec2 point, YAGBAR_Ray ray)
{
    YAGBAR_Unit dX = point.x - ray.start.x;
    YAGBAR_Unit dY = point.y - ray.start.y;
    return (ray.direction.x * dY - ray.direction.y * dX) > 0;
        /* ^ Z component of cross-product */
}


#endif /* YAGBAR_MATH_H */
