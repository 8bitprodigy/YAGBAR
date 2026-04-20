#ifndef YAGBAR_CORE_H
#define YAGBAR_CORE_H


#include <tonc.h>

#include "defs.h"


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
YAGBAR_Vec2;

typedef struct
{
    YAGBAR_Vec2 position;
    YAGBAR_Unit     angle;  
    YAGBAR_Vec2 resolution;
    /*  Shear offset in pixels (0 => no shear), can simulate
        looking up/down. */
    s16             shear; 
    YAGBAR_Unit     height;
} 
YAGBAR_Camera;

typedef struct
{
    YAGBAR_Vec2 start;
    YAGBAR_Vec2 direction;
} 
YAGBAR_Ray;

typedef struct
{
    u16 max_hits;
    u16 max_steps;
} 
YAGBAR_RayConstraints;

typedef struct
{
    YAGBAR_Vec2 position;
    YAGBAR_Unit z;
    YAGBAR_Unit radius;
    u8          kind;
    u8          state;
    u8          health;
    u8          armor;
    u8          target;
    u8          sprite_index;
    union {
        u8 flags;
        struct {
            bool visible   :1;
            bool flip_x    :1;
            bool flip_y    :1;
            bool fullbright:1;
        };
    };
}
YAGBAR_Entity;


void 
YAGBAR_initCamera(YAGBAR_Camera *camera);
void 
YAGBAR_initRayConstraints(YAGBAR_RayConstraints *constraints);
YAGBAR_Unit
YAGBAR_heightAt(s16 x, s16 y);


extern YAGBAR_Entity YAGBAR_entities[YAGBAR_MAX_ENTITIES];
extern u8            YAGBAR_entityCount;


#endif /* YAGBAR_CORE_H */
