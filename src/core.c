#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "core.h"
#include "data.h"
#include "render.h"


/*******************************************************************************
    PRIVATE VARIABLES
*******************************************************************************/
EWRAM_DATA
YGR_Entity YGR_entities[YGR_MAX_ENTITIES] = {0};
u8         YGR_entityCount                =  0;
u32        YGR_timeRollover               =  0;
u32        YGR_prevTime                   =  0;
u32        YGR_globalTime                 =  0;
YGR_Unit   YGR_deltaTime                  =  0;


#ifdef DEBUG
#include "mgba.c"
#endif /* DEBUG */


__attribute__((constructor))
void
YGR_init(void)
{
    REG_TM0CNT = TM_FREQ_64  | TM_ENABLE;
    REG_TM1CNT = TM_CASCADE  | TM_ENABLE;
}

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


/*******************************************************************************
    YGR_Thinker METHODS
*******************************************************************************/
void
YGR_tick(void)
{
    register u32 current  = (REG_TM1D << 16) | REG_TM0D;
    YGR_deltaTime         = current - YGR_prevTime;
    register u32 new_time = YGR_globalTime + YGR_deltaTime;
    if (new_time < YGR_globalTime) YGR_timeRollover++;
    YGR_globalTime        = new_time;
    YGR_prevTime          = current;
}


/*******************************************************************************
    YGR_Thinker METHODS
*******************************************************************************/
void
YGR_Thinker_init(YGR_Thinker *thinker)
{
    
}

void
YGR_Thinker_set(
    YGR_Thinker         *thinker,
    YGR_ThinkerFunction *function,
    YGR_Unit             delay,
    void                *data
)
{
    
}

void
YGR_Thinker_repeat(
    YGR_Thinker         *thinker,
    YGR_ThinkerFunction *function,
    YGR_Unit             interval,
    void                *data
)
{
    
}

void
YGR_Thinker_update(YGR_Entity *entity, u32 current_time)
{
    
}
