#include "math.h"
#include "reciprocal_table.h"


#if MATH_USE_COS_LUT == 1
const 
YGR_Unit 
cosLUT[64] =
{
    1024,1019,1004,979,946,903,851,791,724,649,568,482,391,297,199,100,0,-100,
    -199,-297,-391,-482,-568,-649,-724,-791,-851,-903,-946,-979,-1004,-1019,
    -1023,-1019,-1004,-979,-946,-903,-851,-791,-724,-649,-568,-482,-391,-297,
    -199,-100,0,100,199,297,391,482,568,649,724,791,851,903,946,979,1004,1019
};

#elif MATH_USE_COS_LUT == 2
const 
YGR_Unit 
cosLUT[128] =
{
    1024,1022,1019,1012,1004,993,979,964,946,925,903,878,851,822,791,758,724,
    687,649,609,568,526,482,437,391,344,297,248,199,150,100,50,0,-50,-100,-150,
    -199,-248,-297,-344,-391,-437,-482,-526,-568,-609,-649,-687,-724,-758,-791,
    -822,-851,-878,-903,-925,-946,-964,-979,-993,-1004,-1012,-1019,-1022,-1023,
    -1022,-1019,-1012,-1004,-993,-979,-964,-946,-925,-903,-878,-851,-822,-791,
    -758,-724,-687,-649,-609,-568,-526,-482,-437,-391,-344,-297,-248,-199,-150,
    -100,-50,0,50,100,150,199,248,297,344,391,437,482,526,568,609,649,687,724,
    758,791,822,851,878,903,925,946,964,979,993,1004,1012,1019,1022
};
#endif /* MATH_USE_COS_LUT */


IWRAM_CODE 
YGR_Unit 
MATH_tan(YGR_Unit input)
{
    return MATH_fast_div((MATH_sin(input) * YGR_UNITS_PER_SQUARE), MATH_nonZero(MATH_cos(input)));
}

IWRAM_CODE 
YGR_Unit 
MATH_ctg(YGR_Unit input)
{
    return MATH_fast_div((MATH_cos(input) * YGR_UNITS_PER_SQUARE), MATH_sin(input));
}

IWRAM_CODE 
YGR_Vec2 
MATH_angleToDirection(YGR_Unit angle)
{
    YGR_Vec2 result;

    result.x = MATH_cos(angle);
    result.y = -1 * MATH_sin(angle);

    return result;
}

IWRAM_CODE 
u16 
MATH_sqrt(YGR_Unit value)
{
    u32 
        result = 0,
        a      = value,
        b      = 1u << 30;
    
    while (b > a)
        b >>= 2;

    while (b != 0) {
        if (a >= result + b) {
            a -= result + b;
            result = result +  2 * b;
        }

        b >>= 2;
        result >>= 1;
    }

    return result;
}

IWRAM_CODE
YGR_Unit 
MATH_dist(YGR_Vec2 p1, YGR_Vec2 p2)
{
    YGR_Unit 
        dx = p2.x - p1.x,
        dy = p2.y - p1.y;

#if MATH_USE_DIST_APPROX == 2
    // octagonal approximation

    dx = MATH_abs(dx);
    dy = MATH_abs(dy);

    return dy > dx ? (dx >> 1) + dy : (dy >> 1) + dx;
#elif MATH_USE_DIST_APPROX == 1
    // more accurate approximation

    YGR_Unit a, b, result;

    dx = ((dx < 0) * 2 - 1) * dx;
    dy = ((dy < 0) * 2 - 1) * dy;

    if (dx < dy) {
        a = dy;
        b = dx;
    }
    else {
        a = dx;
        b = dy;
    }

    result = a + MATH_fast_div((44 * b), 102);

    if (a < (b << 4))
        result -= (5 * a) >> 7;

    return result;
#else
    dx = dx * dx;
    dy = dy * dy;

    return MATH_sqrt((YGR_Unit) (dx + dy));
#endif /* MATH_USE_DIST_APPROX */
}







