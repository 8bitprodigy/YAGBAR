#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "core.h"
#include "data.h"
#include "render.h"



YGR_Entity YGR_entities[YGR_MAX_ENTITIES] = {0};
u8            YGR_entityCount            =  0;


#ifdef DEBUG
#include "mgba.c"
#endif /* DEBUG */

void 
YGR_initCamera(YGR_Camera *camera)
{
    camera->position.x   = 0;
    camera->position.y   = 0;
    camera->angle        = 0;
    camera->resolution.x = 20;
    camera->resolution.y = 15;
    camera->shear        = 0;
    camera->height       = YGR_UNITS_PER_SQUARE;
}

void 
YGR_initRayConstraints(YGR_RayConstraints *constraints)
{
    constraints->max_hits  = 1;
    constraints->max_steps = 20;
}

YGR_Unit 
YGR_heightAt(s16 x, s16 y)
{
    YGR_Unit index = y * LEVEL_W + x;
    if (index < 0 || index >= LEVEL_W * LEVEL_H)
        return YGR_UNITS_PER_SQUARE * 2;   // treat out-of-bounds as wall
    return level[index] * YGR_UNITS_PER_SQUARE * 2;
}
