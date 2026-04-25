#ifndef YGR_CORE_H
#define YGR_CORE_H


#include <tonc.h>

#include "defs.h"


// Log levels
#define MGBA_LOG_FATAL   0
#define MGBA_LOG_ERROR   1
#define MGBA_LOG_WARN    2
#define MGBA_LOG_INFO    3
#define MGBA_LOG_DEBUG   4


#ifdef DEBUG
    #define DBG_EXPR( ... )        __VA_ARGS__
    #define DBG_OUT(  Text, ... )  mgba_printf(MGBA_LOG_DEBUG, Text, ##__VA_ARGS__ )
    #define ERR_OUT(  Error_Text ) mgba_printf(MGBA_LOG_ERROR, Text, ##__VA_ARGS__ )
#else
    #define DBG_EXPR( ... ) 
    #define DBG_OUT(  Text, ... )
    #define ERR_OUT(  Error_Text )
#endif /* DEBUG */


/*  Smallest spatial unit, there is
	YGR_UNITS_PER_SQUARE units in a square's
	length. This effectively serves the purpose of
	a fixed-point arithmetic. */
typedef s32 YGR_Unit; 

/* Position in 2D space. */
typedef struct
{
    YGR_Unit x, y;
} 
YGR_Vec2;

typedef struct
{
    YGR_Vec2 position;
    YGR_Vec2 resolution;
    YGR_Unit angle;  
    YGR_Unit height;
    /*  Shear offset in pixels (0 => no shear), can simulate
        looking up/down. */
    s16      shear; 
} 
YGR_Camera;

typedef struct
{
    YGR_Vec2 start;
    YGR_Vec2 direction;
} 
YGR_Ray;

typedef struct
{
    u16 
        max_hits,
        max_steps;
} 
YGR_RayConstraints;


/*  THINKER
    The "brain" of enemy AI.
*/
typedef struct YGR_Entity;
typedef void(*YGR_ThinkerFunction)(struct YGR_Entity *entity, void *user_data);
typedef struct
{
    YGR_ThinkerFunction  function;
    void                *data;
    u32                  next_time;
    YGR_Unit             interval;
}
YGR_Thinker;


typedef struct
{
    YGR_Thinker thinker;
    YGR_Vec2    position;
    YGR_Unit 
        z,
        radius;
    u16         sprite_index;
    u8
        kind,
        state,
        health,
        armor,
        target;
    union {
        u8 flags;
        struct {
            bool 
                solid     :1,
                visible   :1,
                flip_x    :1,
                flip_y    :1,
                fullbright:1;
        };
    };
}
YGR_Entity;


#ifdef DEBUG
void 
YGR_printf(int level, const char* ptr, ...);
#endif /* DEBUG */

void 
YGR_initCamera(YGR_Camera *camera);
void 
YGR_initRayConstraints(YGR_RayConstraints *constraints);

YGR_Unit
YGR_heightAt(s16 x, s16 y);

/*
    TIMER METHODS
*/
extern YGR_Unit YGR_deltaTime;
extern u32      YGR_globalTime;
void
YGR_tick(void);

/*
    THINKER METHODS
*/
void
YGR_Thinker_init(YGR_Thinker *thinker);
void
YGR_Thinker_set(
    YGR_Thinker         *thinker,
    YGR_ThinkerFunction *function,
    YGR_Unit             delay,
    void                *data
);
void
YGR_Thinker_repeat(
    YGR_Thinker         *thinker,
    YGR_ThinkerFunction *function,
    YGR_Unit             interval,
    void                *data
);
void
YGR_Thinker_update(YGR_Entity *entity, u32 current_time);


extern YGR_Entity YGR_entities[YGR_MAX_ENTITIES];
extern u8            YGR_entityCount;


#endif /* YGR_CORE_H */
