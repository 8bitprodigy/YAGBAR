#ifndef YGR_MATH_H
#define YGR_MATH_H


#include <tonc.h>

#include "core.h"
#include "mgba.h"
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
#define MATH_min(a,b)        ((b) + (((a)-(b)) & (((a)-(b)) >> 31)))
#define MATH_max(a,b)        ((a) - (((a)-(b)) & (((a)-(b)) >> 31)))
#define MATH_nonZero(v)      ((v) + ((v) == 0)) ///< To prevent zero divisions.
#define MATH_zeroClamp(x)    ((x) * ((x) >= 0))
#define MATH_likely(cond)    __builtin_expect(!!(cond),1) 
#define MATH_unlikely(cond)  __builtin_expect(!!(cond),0) 

// Bhaskara's cosine approximation formula
#define trigHelper(x) (((YGR_Unit) YGR_UNITS_PER_SQUARE) *\
    ((YGR_UNITS_PER_SQUARE >> 1) * (YGR_UNITS_PER_SQUARE >> 1) - 4 * (x) * (x)) /\
    ((YGR_UNITS_PER_SQUARE >> 1) * (YGR_UNITS_PER_SQUARE >> 1) + (x) * (x)))


#ifdef YGR_DEBUG_MATH
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
#endif /* YGR_DEBUG_MATH */

/*******************************************************************************
    GLOBALS
*******************************************************************************/
#if MATH_USE_COS_LUT != 0
extern const YGR_Unit cosLUT[];
#endif /* MATH_USE_COS_LUT */


/*******************************************************************************
    PUBLIC METHODS / FUNCTIONS
*******************************************************************************/
static inline
YGR_Unit
MATH_fast_div(YGR_Unit numerator, YGR_Unit denominator)
{
    if ((u32)denominator < DEPTH_RECIPROCAL_SIZE)
        return (YGR_Unit)(((s32)numerator * depth_reciprocal[denominator]) >> 10);
    if (0 < numerator)
        switch (denominator) {
        case 1024:  return numerator >> 10;
        case 2048:  return numerator >> 11;
        case 4096:  return numerator >> 12;
        case 8192:  return numerator >> 13;
        case 16384: return numerator >> 14;
        case 32768: return numerator >> 15;
        case 65536: return numerator >> 16;
        }
    else {
        register YGR_Unit num = -numerator;
        switch (denominator) {
        case 1024:  return -(num >> 10);
        case 2048:  return -(num >> 11);
        case 4096:  return -(num >> 12);
        case 8192:  return -(num >> 13);
        case 16384: return -(num >> 14);
        case 32768: return -(num >> 15);
        case 65536: return -(num >> 16);
        }
    }
    return numerator / denominator;
}

static inline
YGR_Unit
MATH_fast_mod(YGR_Unit numerator, YGR_Unit modulus)
{
    if ((u32)modulus < DEPTH_RECIPROCAL_SIZE) {
        register s32 prod = MATH_fast_div(numerator, modulus);
        return numerator - ((prod >> 10) * modulus);
    }
    return numerator % modulus;
}

/// Like mod, but behaves differently for negative values. 
static inline 
YGR_Unit 
MATH_wrap(YGR_Unit value, YGR_Unit mod)
{
    YGR_Unit cmp = value < 0;
    return cmp * mod + MATH_fast_mod(value, mod) - cmp;
}
 
static inline
YGR_Unit 
MATH_clamp(YGR_Unit value, YGR_Unit valueMin, YGR_Unit valueMax)
{
    return (value < valueMin) 
            * valueMin 
            + (
                    value >= valueMin 
                    && value <= valueMax
                ) 
            * value 
            + (value > valueMax) 
            * valueMax;
}

/// Performs division, rounding down, NOT towards zero. 
static inline 
YGR_Unit 
MATH_divRoundDown(YGR_Unit value, YGR_Unit divisor)
{
    return MATH_fast_div(value, divisor) - ((value >= 0) ? 0 : 1);
}
 
static inline 
YGR_Unit 
MATH_abs(YGR_Unit value)
{
    return value * (((value >= 0) << 1) - 1);
}
 
YGR_Vec2 
MATH_angleToDirection(YGR_Unit angle);

/*  Cos function.

    @param  input to cos in YGR_Units (YGR_UNITS_PER_SQUARE = 2 * pi = 360 degrees)
    @return MATH_normalized output in YGR_Units (from -YGR_UNITS_PER_SQUARE to
        YGR_UNITS_PER_SQUARE)
*/
static inline
YGR_Unit 
MATH_cos(YGR_Unit input)
{
    input = MATH_wrap(input,YGR_UNITS_PER_SQUARE);

#if MATH_USE_COS_LUT == 1

    #ifdef RCL_RAYCAST_TINY
        return cosLUT[input];
    #else
        return cosLUT[input >> 4];
    #endif

#elif MATH_USE_COS_LUT == 2
    return cosLUT[input >> 3];
#else
    if (input < YGR_UNITS_PER_SQUARE >> 2)
        return trigHelper(input);
    else if (input < YGR_UNITS_PER_SQUARE >> 1)
        return -1 * trigHelper((YGR_UNITS_PER_SQUARE >> 1) - input);
    else if (input < 3 * YGR_UNITS_PER_SQUARE >> 2)
        return -1 * trigHelper(input - (YGR_UNITS_PER_SQUARE >> 1));
    else
        return trigHelper(YGR_UNITS_PER_SQUARE - input);
#endif
}

#undef trigHelper


static inline
YGR_Unit 
MATH_sin(YGR_Unit input)
{
    return MATH_cos(input - (YGR_UNITS_PER_SQUARE >> 2));
}


YGR_Unit 
MATH_tan(YGR_Unit input);

YGR_Unit 
MATH_ctg(YGR_Unit input);


uint16_t 
MATH_sqrt(YGR_Unit value);

YGR_Unit 
MATH_dist(YGR_Vec2 p1, YGR_Vec2 p2);

IWRAM_CODE
static inline
YGR_Unit 
MATH_len(YGR_Vec2 v)
{
    YGR_Vec2 zero;
    zero.x = 0;
    zero.y = 0;

    return MATH_dist(zero,v);
}


/// Normalizes given vector to have YGR_UNITS_PER_SQUARE length. 
static inline
YGR_Vec2 
MATH_normalize(YGR_Vec2 v)
{
    YGR_Vec2 result;
    YGR_Unit l = MATH_len(v);
    l = MATH_nonZero(l);

    result.x = MATH_fast_div((v.x * YGR_UNITS_PER_SQUARE), l);
    result.y = MATH_fast_div((v.y * YGR_UNITS_PER_SQUARE), l);

    return result;
}

/// Computes a cos of an angle between two vectors. 
static inline
YGR_Unit 
MATH_vectorsAngleCos(YGR_Vec2 v1, YGR_Vec2 v2)
{
    v1 = MATH_normalize(v1);
    v2 = MATH_normalize(v2);

    return MATH_fast_div((v1.x * v2.x + v1.y * v2.y), YGR_UNITS_PER_SQUARE);
}

inline
YGR_Unit 
MATH_clamp(YGR_Unit value, YGR_Unit valueMin, YGR_Unit valueMax);

/*  Converts an angle in whole degrees to an angle in YGR_Units that this 
    engine uses.
*/    
static inline
YGR_Unit 
MATH_degreesToUnitsAngle(int16_t degrees)
{
    return MATH_fast_div((degrees * YGR_UNITS_PER_SQUARE), 360);
}

static inline 
int8_t 
MATH_pointIsLeftOfRay(YGR_Vec2 point, YGR_Ray ray)
{
    YGR_Unit dX = point.x - ray.start.x;
    YGR_Unit dY = point.y - ray.start.y;
    return (ray.direction.x * dY - ray.direction.y * dX) > 0;
        /* ^ Z component of cross-product */
}


#endif /* YGR_MATH_H */
