#ifndef YAGBAR_CORE_H
#define YAGBAR_CORE_H


#include <tonc.h>

#include "defs.h"


#ifndef YAGBAR_BITS_PRECISION
	#define YAGBAR_BITS_PRECISION (10)
#endif
/*  Number of YAGBAR_Units in a side of a
	spatial square, i.e. the fixed point
	scaling. */
#define YAGBAR_UNITS_PER_SQUARE (1 << YAGBAR_BITS_PRECISION) 

#define YAGBAR_INFINITY 2000000000
#define YAGBAR_U        YAGBAR_UNITS_PER_SQUARE ///< shorthand for YAGBAR_UNITS_PER_SQUARE


/*  Smallest spatial unit, there is
	YAGBAR_UNITS_PER_SQUARE units in a square's
	length. This effectively serves the purpose of
	a fixed-point arithmetic. */
typedef s32 YAGBAR_Unit; 

/* Position in 2D space. */
typedef struct
{
    YAGBAR_Unit x;
    YAGBAR_Unit y;
} 
YAGBAR_Vector2D;

typedef struct
{
    YAGBAR_Vector2D position;
    YAGBAR_Unit     angle;  
    YAGBAR_Vector2D resolution;
    /*  Shear offset in pixels (0 => no shear), can simulate
        looking up/down. */
    s16             shear; 
    YAGBAR_Unit     height;
} 
YAGBAR_Camera;

typedef struct
{
    YAGBAR_Vector2D start;
    YAGBAR_Vector2D direction;
} 
YAGBAR_Ray;

typedef struct
{
    uint16_t max_hits;
    uint16_t max_steps;
} 
YAGBAR_RayConstraints;


void 
YAGBAR_initCamera(YAGBAR_Camera *camera);
void 
YAGBAR_initRayConstraints(YAGBAR_RayConstraints *constraints);
YAGBAR_Unit
YAGBAR_heightAt(s16 x, s16 y);



#endif /* YAGBAR_CORE_H */
