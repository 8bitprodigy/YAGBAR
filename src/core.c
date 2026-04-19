#include "core.h"
#include "data.h"
#include "render.h"


void 
YAGBAR_initCamera(YAGBAR_Camera *camera)
{
    camera->position.x   = 0;
    camera->position.y   = 0;
    camera->angle        = 0;
    camera->resolution.x = 20;
    camera->resolution.y = 15;
    camera->shear        = 0;
    camera->height       = YAGBAR_UNITS_PER_SQUARE;
}

void 
YAGBAR_initRayConstraints(YAGBAR_RayConstraints *constraints)
{
    constraints->max_hits  = 1;
    constraints->max_steps = 20;
}

inline
YAGBAR_Unit 
YAGBAR_heightAt(s16 x, s16 y)
{
    YAGBAR_Unit index = y * LEVEL_W + x;
    if (index < 0 || index >= LEVEL_W * LEVEL_H)
        return YAGBAR_UNITS_PER_SQUARE * 2;   // treat out-of-bounds as wall
    return level[index] * YAGBAR_UNITS_PER_SQUARE * 2;
}
