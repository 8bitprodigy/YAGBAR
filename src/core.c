#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "core.h"
#include "data.h"
#include "math.h"
#include "render.h"


typedef struct {
    u8  count;
    u32 mask[YGR_MAX_ENTITIES>>5];
}
GridCell;

/*******************************************************************************
    PRIVATE VARIABLES
*******************************************************************************/
EWRAM_DATA
YGR_Entity YGR_entities[YGR_MAX_ENTITIES] = {0};
EWRAM_DATA
GridCell   YGR_spatialGrid[16][16]        = {0};
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
        return YGR_UNITS_PER_SQUARE;   // treat out-of-bounds as wall
    return level[index] * YGR_UNITS_PER_SQUARE;
}


/*******************************************************************************
    YAGBAR Methods
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

void
YGR_moveEntity(
    s8                *level, 
    YGR_Entity        *entity, 
    YGR_Vec2           destination, 
    YGR_CollisionInfo *info
)
{
    u8       slides    = entity->slides;
    YGR_Vec2 current   = entity->position;
    YGR_Vec2 delta     = { destination.x - current.x, destination.y - current.y };
    
    if (info) {
        info->entity   = NULL;
        info->end_or_wall = false;
    }
    
    for (s8 i = entity->slides; 0 <= i; i--) {
        YGR_Unit   earliest_t  = YGR_UNITS_PER_SQUARE; // 1.0 in fixed point
        bool       hit         = false;
        YGR_Vec2   hit_normal  = { 0, 0 };
        YGR_Vec2   hit_pos     = { 0, 0 };
        bool       hit_is_wall = false;
        YGR_Entity *hit_entity = NULL;

        // --- Wall collision (swept AABB) ---
        YGR_Unit radius = entity->radius;

        // X axis sweep
        if (delta.x != 0) {
            YGR_Unit edge_x = (delta.x > 0)
                ? current.x + radius + delta.x
                : current.x - radius + delta.x;
            s16 tile_x = MATH_divRoundDown(edge_x, YGR_UNITS_PER_SQUARE);
            s16 tile_y0 = MATH_divRoundDown(current.y - radius, YGR_UNITS_PER_SQUARE);
            s16 tile_y1 = MATH_divRoundDown(current.y + radius - 1, YGR_UNITS_PER_SQUARE);

            for (s16 ty = tile_y0; ty <= tile_y1; ty++) {
                if (level[ty * LEVEL_W + tile_x] != 0) {
                    // Find t at which we hit this wall
                    YGR_Unit wall_x = (delta.x > 0)
                        ? (tile_x << YGR_BITS_PRECISION) - radius
                        : ((tile_x + 1) << YGR_BITS_PRECISION) + radius;
                    YGR_Unit t = ((wall_x - current.x) << YGR_BITS_PRECISION) / delta.x;
                    if (t >= 0 && t < earliest_t) {
                        earliest_t  = t;
                        hit         = true;
                        hit_is_wall = true;
                        hit_normal  = (YGR_Vec2){ (delta.x > 0) ? -1 : 1, 0 };
                        hit_pos     = (YGR_Vec2){
                            current.x + ((delta.x * t) >> YGR_BITS_PRECISION),
                            current.y + ((delta.y * t) >> YGR_BITS_PRECISION)
                        };
                    }
                    break;
                }
            }
        }

        // Y axis sweep
        if (delta.y != 0) {
            YGR_Unit edge_y = (delta.y > 0)
                ? current.y + radius + delta.y
                : current.y - radius + delta.y;
            s16 tile_y = MATH_divRoundDown(edge_y, YGR_UNITS_PER_SQUARE);
            s16 tile_x0 = MATH_divRoundDown(current.x - radius, YGR_UNITS_PER_SQUARE);
            s16 tile_x1 = MATH_divRoundDown(current.x + radius - 1, YGR_UNITS_PER_SQUARE);

            for (s16 tx = tile_x0; tx <= tile_x1; tx++) {
                if (level[tile_y * LEVEL_W + tx] != 0) {
                    YGR_Unit wall_y = (delta.y > 0)
                        ? (tile_y << YGR_BITS_PRECISION) - radius
                        : ((tile_y + 1) << YGR_BITS_PRECISION) + radius;
                    YGR_Unit t = ((wall_y - current.y) << YGR_BITS_PRECISION) / delta.y;
                    if (t >= 0 && t < earliest_t) {
                        earliest_t  = t;
                        hit         = true;
                        hit_is_wall = true;
                        hit_normal  = (YGR_Vec2){ 0, (delta.y > 0) ? -1 : 1 };
                        hit_pos     = (YGR_Vec2){
                            current.x + ((delta.x * t) >> YGR_BITS_PRECISION),
                            current.y + ((delta.y * t) >> YGR_BITS_PRECISION)
                        };
                    }
                    break;
                }
            }
        }

        // --- Entity collision (swept AABB against spatial grid) ---
        s16 cell_x0 = MATH_divRoundDown(MIN(current.x, destination.x) - radius, 4 * YGR_UNITS_PER_SQUARE) >> 2;
        s16 cell_y0 = MATH_divRoundDown(MIN(current.y, destination.y) - radius, 4 * YGR_UNITS_PER_SQUARE) >> 2;
        s16 cell_x1 = MATH_divRoundDown(MAX(current.x, destination.x) + radius, 4 * YGR_UNITS_PER_SQUARE) >> 2;
        s16 cell_y1 = MATH_divRoundDown(MAX(current.y, destination.y) + radius, 4 * YGR_UNITS_PER_SQUARE) >> 2;

        cell_x0 = MAX(0, MIN(15, cell_x0));
        cell_y0 = MAX(0, MIN(15, cell_y0));
        cell_x1 = MAX(0, MIN(15, cell_x1));
        cell_y1 = MAX(0, MIN(15, cell_y1));

        for (s16 cy = cell_y0; cy <= cell_y1; cy++) {
            for (s16 cx = cell_x0; cx <= cell_x1; cx++) {
                GridCell *cell = &YGR_spatialGrid[cy][cx];
                if (cell->count == 0) continue;

                for (int word = 0; word < 8; word++) {
                    u32 bits = cell->mask[word];
                    while (bits) {
                        int      bit    = __builtin_ctz(bits);
                        int      id     = (word << 5) | bit;
                        YGR_Entity *other = &YGR_entities[id];
                        bits &= bits - 1;

                        if (other == entity) continue;

                        // Swept AABB
                        YGR_Unit combined = entity->radius + other->radius;
                        YGR_Vec2 rel      = {
                            current.x - other->position.x,
                            current.y - other->position.y
                        };

                        // Check X entry
                        if (delta.x != 0) {
                            YGR_Unit entry_x = (delta.x > 0)
                                ? ( combined - rel.x) << YGR_BITS_PRECISION
                                : (-combined - rel.x) << YGR_BITS_PRECISION;
                            YGR_Unit t = entry_x / delta.x;
                            if (t >= 0 && t < earliest_t) {
                                // Verify Y overlap at this t
                                YGR_Unit y_at_t = rel.y + ((delta.y * t) >> YGR_BITS_PRECISION);
                                if (y_at_t > -combined && y_at_t < combined) {
                                    earliest_t  = t;
                                    hit         = true;
                                    hit_is_wall = false;
                                    hit_entity  = other;
                                    hit_normal  = (YGR_Vec2){ (delta.x > 0) ? -1 : 1, 0 };
                                    hit_pos     = (YGR_Vec2){
                                        current.x + ((delta.x * t) >> YGR_BITS_PRECISION),
                                        current.y + ((delta.y * t) >> YGR_BITS_PRECISION)
                                    };
                                }
                            }
                        }

                        // Check Y entry
                        if (delta.y != 0) {
                            YGR_Unit entry_y = (delta.y > 0)
                                ? ( combined - rel.y) << YGR_BITS_PRECISION
                                : (-combined - rel.y) << YGR_BITS_PRECISION;
                            YGR_Unit t = entry_y / delta.y;
                            if (t >= 0 && t < earliest_t) {
                                YGR_Unit x_at_t = rel.x + ((delta.x * t) >> YGR_BITS_PRECISION);
                                if (x_at_t > -combined && x_at_t < combined) {
                                    earliest_t  = t;
                                    hit         = true;
                                    hit_is_wall = false;
                                    hit_entity  = other;
                                    hit_normal  = (YGR_Vec2){ 0, (delta.y > 0) ? -1 : 1 };
                                    hit_pos     = (YGR_Vec2){
                                        current.x + ((delta.x * t) >> YGR_BITS_PRECISION),
                                        current.y + ((delta.y * t) >> YGR_BITS_PRECISION)
                                    };
                                }
                            }
                        }
                    }
                }
            }
        }

        if (!hit) {
            // No collision, move freely
            entity->position = destination;
            break;
        }

        // Move to contact point
        entity->position = hit_pos;
        current          = hit_pos;

                // Populate info on first hit only
        if (info && !info->end_or_wall && !info->entity) {
            info->position    = hit_pos;
            info->normal      = hit_normal;
            info->end_or_wall = hit_is_wall;
            if (hit_is_wall)
                info->wall   = NULL;
            else
                info->entity = hit_entity;
        }

        if (slides == 0) break;
        slides--;

        if (hit_is_wall) {
            if (hit_normal.x != 0) {
                destination.x = hit_pos.x;
                delta.x       = 0;
                delta.y       = destination.y - hit_pos.y;
            } else {
                destination.y = hit_pos.y;
                delta.y       = 0;
                delta.x       = destination.x - hit_pos.x;
            }
        } else {
            // Resolve entity-entity
            if (hit_entity != NULL) {
                YGR_Unit overlap = (entity->radius + hit_entity->radius)
                                 - (YGR_Unit)MATH_sqrt(
                                     (current.x - hit_entity->position.x) * (current.x - hit_entity->position.x) +
                                     (current.y - hit_entity->position.y) * (current.y - hit_entity->position.y)
                                 );
                entity->position.x    += (hit_normal.x * overlap) >> 1;
                entity->position.y    += (hit_normal.y * overlap) >> 1;
                hit_entity->position.x -= (hit_normal.x * overlap) >> 1;
                hit_entity->position.y -= (hit_normal.y * overlap) >> 1;
                current = entity->position;
            }

            YGR_Unit dot = (destination.x - current.x) * hit_normal.x + (destination.y - current.y) * hit_normal.y;
            delta.x       = destination.x - current.x - dot * hit_normal.x;
            delta.y       = destination.y - current.y - dot * hit_normal.y;
            destination.x = current.x + delta.x;
            destination.y = current.y + delta.y;
        }

        if (delta.x == 0 && delta.y == 0) break;
    }
}



/*******************************************************************************
    YGR_Thinker METHODS
*******************************************************************************/
void
YGR_Thinker_init(YGR_Thinker *thinker)
{
    thinker->function  = NULL;
    thinker->data = NULL;
    thinker->next_time = YGR_INFINITY;
    thinker->interval  = 0;
}

void
YGR_Thinker_set(
    YGR_Thinker         *thinker,
    YGR_ThinkerFunction  function,
    YGR_Unit             delay,
    void                *data
)
{
    thinker->function  = function;
    thinker->data      = data;
    thinker->interval  = -delay;
    thinker->next_time = 0;
}

void
YGR_Thinker_repeat(
    YGR_Thinker         *thinker,
    YGR_ThinkerFunction  function,
    YGR_Unit             interval,
    void                *data
)
{
    thinker->function  = function;
    thinker->data      = data;
    thinker->interval  = interval;
    thinker->next_time = 0;
}

void
YGR_Thinker_update(YGR_Entity *entity, u32 current_time)
{
    YGR_Thinker *thinker = (YGR_Thinker*)entity;
    if (!thinker->function) return;

    register YGR_Unit delta    = YGR_deltaTime;
    register YGR_Unit interval = thinker->interval;
    
    thinker->next_time += delta;

    if (thinker->next_time < MATH_abs(interval)) return;

    thinker->next_time = 0;
    thinker->function(entity, thinker->data);

    if (interval < 0) { /* If one-shot */
        thinker->function = NULL;
        thinker->data     = NULL;
    }
}
