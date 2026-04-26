#include "core.h"
#include "data.h"
#include "math.h"
#include "mgba.h"
#include "palette.h"
#include "render.h"
#include "reciprocal_table.h"


#define _RENDER_UNUSED(what) (void)(what);


static inline void pixelFunc(RENDER_PixelInfo *pixel);

#ifdef RENDER_COLUMN_FUNCTION
static inline void 
RENDER_COLUMN_FUNCTION(
    YGR_Unit x, 
    YGR_Unit y_start, 
    YGR_Unit y_end, 
    u8          is_floor
);
#endif /* RENDER_COLUMN_FUNCTION */

YGR_Unit RENDER_RS_HEIGHT_FN(s16 x, s16 y);


/*******************************************************************************
	Global helper variables, for precomputing stuff etc.
*******************************************************************************/
static u8                    _RENDER_visibleSprites[RENDER_NUM_VISIBLE_SPRITES];
static YGR_Unit              _RENDER_spriteDist[RENDER_NUM_VISIBLE_SPRITES];
static u8                    _RENDER_spriteCount                = 0;
static YGR_Unit              _RENDER_depthBuf[2][RENDER_W]      = {0};
static YGR_Unit              _RENDER_flatsDepthBuf[2][SCREEN_H] = {0};
static u8                    _RENDER_wallTopBuf[2][RENDER_W]    = {0};
static u8                    _RENDER_wallTopMin[2]              = {0};
static u8                    _RENDER_wallBotBuf[2][RENDER_W]    = {0};
static u8                    _RENDER_wallBotMin[2]              = {0};
static s8                    _RENDER_flatsShadeBuf[2][SCREEN_H] = {0};
static YGR_Camera           *_RENDER_camera                     = NULL;
static RENDER_ArrayFunction  _RENDER_floorFunction              = NULL;
static RENDER_ArrayFunction  _RENDER_ceilFunction               = NULL;
static RENDER_ArrayFunction  _RENDER_rollFunction               = NULL; // says door rolling
static YGR_Unit             *_RENDER_floorPixelDistances        = NULL;
static u16                  *_RENDER_drawBuf                    = NULL;
static YGR_Unit              _RENDER_horizontalDepthStep        = 0; 
static YGR_Unit              _RENDER_startFloorHeight           = 0;
static YGR_Unit              _RENDER_startCeil_Height           = 0;
static YGR_Unit              _RENDER_camResYLimit               = 0;
static s16                   _RENDER_middleRow                  = 0;
static YGR_Unit              _RENDER_fHorizontalDepthStart      = 0;
static YGR_Unit              _RENDER_cHorizontalDepthStart      = 0;
static s16                   _RENDER_cameraHeightScreen         = 0;
static YGR_Unit              _RENDER_fovCorrectionFactors[2]    = {0}; //correction for hor/vert fov
static YGR_Unit              _RENDER_fovScale                   = 0;
static u32                   _RENDER_frame                      = 0;
static u8                    _RENDER_page                       = 0;


static inline 
YGR_Unit 
_RENDER_fovCorrectionFactor(YGR_Unit fov);


/*******************************************************************************
    PRIVATE FUNCTIONS
*******************************************************************************/
__attribute__((constructor))
void
RENDER_init(void)
{
    _RENDER_drawBuf = (u16*)(MEM_VRAM + 0xA000);;
    
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < RENDER_W; j++) {
            _RENDER_depthBuf[i][j]   = YGR_INFINITY;
            _RENDER_wallTopBuf[i][j] = 0;
            _RENDER_wallBotBuf[i][j] = SCREEN_H+1;
        }
    }
}

static inline
YGR_Unit 
heightAt(s16 x, s16 y)
{
    YGR_Unit index = y * LEVEL_W + x;
    if (index < 0 || index >= LEVEL_W * LEVEL_H)
        return YGR_UNITS_PER_SQUARE;   // treat out-of-bounds as wall
    return level[index] * YGR_UNITS_PER_SQUARE;
}

IWRAM_CODE
static inline void
sortSprites()
{
    YGR_Vec2 cam_pos = _RENDER_camera->position;
    YGR_Vec2 cam_dir = MATH_angleToDirection(_RENDER_camera->angle);
    _RENDER_spriteCount = 0;
    /* Cull and compute distances */
    for (u8 i = 0; i < YGR_entityCount; i++) {
        YGR_Entity *ent = &YGR_entities[i];

        YGR_Unit
            dx = ent->position.x - cam_pos.x,
            dy = ent->position.y - cam_pos.y;

            /* Cull if behind camera */
            if ((dx * cam_dir.x + dy * cam_dir.y) <= 0) continue;

            _RENDER_spriteDist[_RENDER_spriteCount]     = MATH_abs(dx) + MATH_abs(dy);
            _RENDER_visibleSprites[_RENDER_spriteCount] = i;
            _RENDER_spriteCount++;

            if (_RENDER_spriteCount >= RENDER_NUM_VISIBLE_SPRITES) break;
    }

    /* Insertion sort furthest first */
    for (u8 i = 0; i < _RENDER_spriteCount; i++) {
        u8          key      = _RENDER_visibleSprites[i];
        YGR_Unit key_dist = _RENDER_spriteDist[i];
        s8 j = i - 1;
        while (j >= 0 && _RENDER_spriteDist[j] < key_dist) {
            _RENDER_visibleSprites[j + 1] = _RENDER_visibleSprites[j];
            _RENDER_spriteDist[j + 1]     = _RENDER_spriteDist[j];
            j--;
        }
        _RENDER_visibleSprites[j + 1] = key;
        _RENDER_spriteDist[j + 1]     = key_dist;
    }
}

IWRAM_CODE
static void
_RENDER_drawSprites()
{
    sortSprites();

    YGR_Vec2 cam_pos = _RENDER_camera->position;
    YGR_Vec2 cam_res = _RENDER_camera->resolution;
    YGR_Vec2 cam_dir = MATH_angleToDirection(_RENDER_camera->angle);
    YGR_Unit 
        cam_right_x =  cam_dir.y,
        cam_right_y = -cam_dir.x;

    for (u8 si = 0; si < _RENDER_spriteCount; si++) {
        YGR_Entity *ent = &YGR_entities[_RENDER_visibleSprites[si]];

        /* Transform into camera space */
        YGR_Unit 
            rel_x = ent->position.x - cam_pos.x,
            rel_y = ent->position.y - cam_pos.y,

            depth  = (rel_x * cam_dir.x   + rel_y * cam_dir.y)   
                    / YGR_UNITS_PER_SQUARE,
            strafe = (rel_x * cam_right_x + rel_y * cam_right_y) 
                    / YGR_UNITS_PER_SQUARE;

        if (depth <= 0) continue;

        const YGR_Unit RW = cam_res.x;
        const YGR_Unit RH = cam_res.y;
        
        /* Screen X center */
        s16 screen_x = (RENDER_W >> 1)
            + MATH_fast_div(strafe * (RENDER_W >> 1), depth);

        const u8 *tex = DATA_spriteTextures[ent->sprite_index];
        u8
            spr_w = tex[0],
            spr_h = tex[1];
        const u8 *texture = tex + 2;
        
        /* Screen height */
        s16 sprite_h = (s16)MATH_fast_div(
                RENDER_perspectiveScaleVertical(RH, depth) * spr_h,
                YGR_MAX_SPRITE_HEIGHT
            ) << 1;
        if (sprite_h <= 0) continue;

        s16 sprite_w = (s16)MATH_fast_div(
                RENDER_perspectiveScaleVertical(RH, depth) * spr_w,
                YGR_MAX_SPRITE_WIDTH
            );
        if (sprite_w <= 0) continue;
        
        s16
            draw_x0 = screen_x          - (sprite_w >> 1),
            draw_x1 = screen_x          + (sprite_w >> 1);
        YGR_Unit world_offset = ent->z - _RENDER_camera->height;
        s16 
            screen_center_y = _RENDER_middleRow 
                    - (s16)RENDER_perspectiveScaleVertical(world_offset, depth) 
                    * RH / YGR_UNITS_PER_SQUARE,

            draw_y0 = screen_center_y - (sprite_h >> 1),
            draw_y1 = screen_center_y + (sprite_h >> 1);

        s16 sx0 = -1, sx1 = -1;
        s16 cur0 = -1, cur1 = -1;

        for (s16 sx = draw_x0; sx < draw_x1; sx++) {
            if (sx < 0 || sx >= RW) continue;
            if (depth < _RENDER_depthBuf[_RENDER_page][sx]) {
                if (cur0 == -1) cur0 = sx;
                cur1 = sx;
            } else {
                if (cur0 != -1) {
                    /* keep run closest to center */
                    if (
                        sx0 == -1 
                        || MATH_abs(cur0 + cur1 - 2 * screen_x) 
                        < MATH_abs(sx0 + sx1 - 2 * screen_x)
                    ) {
                        sx0 = cur0;
                        sx1 = cur1;
                    }
                    cur0 = cur1 = -1;
                }
            }
        }

        /* handle run that reaches end */
        if (cur0 != -1) {
            if (
                sx0 == -1 
                || MATH_abs(cur0 + cur1 - 2 * screen_x) 
                < MATH_abs(sx0 + sx1 - 2 * screen_x)
            ) {
                sx0 = cur0;
                sx1 = cur1;
            }
        }

        if (sx0 == -1) continue;

        /* draw horizontally row by row */
        s8  shade   = (s8)MATH_clamp((depth >> DEPTH_SHIFT_AMOUNT), -16, 16);
        s16 texel_w = MATH_fast_div(sprite_w, spr_w);
        register s32 rcp_h = (sprite_h < DEPTH_RECIPROCAL_SIZE) 
                ? depth_reciprocal[sprite_h] 
                : 0;
        register s32 rcp_w = (sprite_w < DEPTH_RECIPROCAL_SIZE) 
                ? depth_reciprocal[sprite_w] 
                : 0;
        
        if (texel_w <= 1) {
            /* When the sprite texel width is 1 pixel or less */
            for (u8 sy = draw_y0; sy < draw_y1; sy++) {
                if (sy < 0 || sy >= RH) continue;

                u8 ty = (u8)(((s32)(sy - draw_y0) * spr_h * rcp_h) >> 10);

                for (u8 sx = sx0; sx <= sx1; sx++) {
                    u8 tx = (u8)(((s32)(sx - draw_x0) * spr_w * rcp_w) >> 10);
                    u8 color = texture[ty * spr_w + tx];
                    if (color == RENDER_PAL_TRANSPARENT) continue;
#if DEPTH_SHADE_SPRITES
/*
                    register u8  base       = color & 0xF0;
                    register s16 shaded     = (s16)color - (s16)shade;
                    shaded                 &= ~(shaded >> 15);
                    register s16 below_base = shaded - (s16)base;
                    shaded                 &= ~(below_base >> 15);
//*/
                    color                   = darkLut[color][shade];//(u8)shaded;
#endif /* DEPTH_SHADE_SPRITES */
                    register u16 *addr = _RENDER_drawBuf + ((sy * (SCREEN_W >> 1)) + sx);
                    *addr     = color | (color << 8);
                }
            }
        }
        else {
            /* When the sprite is magnified */
            for (s16 sy = draw_y0; sy <= draw_y1; sy++) {
                if (sy < 0 || sy >= RH) continue;
                u8 ty = (u8)(((s32)(sy - draw_y0) * spr_h * rcp_h) >> 10);
                s16 sx = sx0;
                DBG_OUT("sx0: %d sx1: %d texel_w: %d sprite_w: %d spr_w: %d", sx0, sx1, texel_w, sprite_w, spr_w);
                register u16 *drawAddr = _RENDER_drawBuf + (sy * (SCREEN_W >> 1));
                while (sx <= sx1) {
                    u8 
                        tx = (u8)(((s32)(sx - draw_x0) * spr_w * rcp_w) >> 10),
                        color   = texture[ty * spr_w + tx];
                    s16 run_end = MATH_clamp(sx + texel_w, 0, RENDER_W);
                    if (color != RENDER_PAL_TRANSPARENT) {
                        register u16 word  = color | (color << 8);
                        register u16 *addr = drawAddr + sx;
                        register u16 *end  = addr + run_end - sx;
                        while (addr < end)
                            *addr++ = word;
                    }
/*
                    s16 run_end = sx + 1;
                    while (run_end < sx1) {
                        s8 next_tx = MATH_fast_div(
                                ((run_end - draw_x0) * spr_w), 
                                sprite_w
                            );
                        if (texture[ty * spr_w + next_tx] != color) break;
                        run_end++;
                    }

                    if (color != RENDER_PAL_TRANSPARENT) {
                        register u8  base        = color & 0xF0;
                        register u16 shaded      = (s16)color - (s16)shade;
                        shaded                  &= ~(shaded >> 15);
                        register s16 below_base  = shaded - (s16)base;
                        shaded                  &= ~(below_base >> 15);
                        color                    = (u8)shaded;
                        u16 word = color | (color << 8);
                        u16 *addr = _RENDER_drawBuf + (sy * (SCREEN_W >> 1)) + sx;
                        u16 *end  = addr + (run_end - sx);
                        while (addr < end)
                            *addr++ = word;
                    }
//*/
                    sx = run_end;
                }
            }
        }
///*
        draw_y0 = MATH_clamp(draw_y0-1, 0, SCREEN_H);
        draw_y1 = MATH_clamp(draw_y1+1, 0, SCREEN_H);
        for (u8 sx = sx0; sx <= sx1; sx++) {
            _RENDER_wallTopBuf[_RENDER_page][sx] = MATH_min(
                    draw_y0, 
                    _RENDER_wallTopBuf[_RENDER_page][sx]
                );
            _RENDER_wallBotBuf[_RENDER_page][sx] = MATH_max(
                    draw_y1,
                    _RENDER_wallBotBuf[_RENDER_page][sx]
                );
        }
//*/
    }
}

IWRAM_CODE
static inline u8
sampleTexture(
  const u8    *tex,
  YGR_Unit  tx, 
  YGR_Unit  ty
)
{
    const u8 w = 64;
    const u8 h = 64;
    tx = MATH_wrap(tx, YGR_UNITS_PER_SQUARE);
    ty = MATH_wrap(ty, YGR_UNITS_PER_SQUARE);
    register s16 px = (w * tx) >> 10;
    register s16 py = (h * ty) >> 10;
    return tex[2 + h * px + py];
}

// Called by wall rendering loop.
IWRAM_CODE 
static inline
void 
_RENDER_wallPixel(RENDER_PixelInfo *p)
{
    register u8 color;
#if (DEPTH_SHADE_WALLS || DEPTH_SHADE_FLOOR || DEPTH_SHADE_CEILING || USE_SIDE_SHADING)
    register u8  base;
    register s16 shaded;
    register s16 below_base;
#endif /* DEPTH_SHADE_WALLS || DEPTH_SHADE_FLOOR || DEPTH_SHADE_CEILING || USE_SIDE_SHADING */


#if !WALLS_TEXTURED    
    // Shade walls by depth and face direction.
    // depth is in RCL units; clamp to 3 shading levels.
    color = PAL_WALL;//3 - RCL_min(3, (int)(p->depth / YGR_UNITS_PER_SQUARE));
#else
    color = sampleTexture(wall_tex, p->texCoords.x, p->texCoords.y);
#endif /* WALLS_UNTEXTURED */


#if (DEPTH_SHADE_WALLS || USE_SIDE_SHADING)
    /* Clamp color to within row */
/*
    base        = color & 0xF0;
    shaded      = (s16)color - (s16)p->shading;
    shaded     &= ~(shaded >> 15);               // clamp to 0 if negative
    below_base  = shaded - (s16)base;
    shaded     &= ~(below_base >> 15);           // clamp to 0 if crossed row boundary
//*/
    color       = darkLut[color][p->shading];//(u8)shaded;
#endif /* DEPTH_SHADE_WALLS || USE_SIDE_SHADING */

    //u16 *addr = _RENDER_drawBuf + (u16)((p->position.y * SCREEN_W + p->position.x * 2) >> 1);
    *p->destination = (u16)color | ((u16)color << 8);
}


IWRAM_CODE 
static inline
void 
_RENDER_flatsPixel(RENDER_PixelInfo *p)
{
    register u8 color;
#if (DEPTH_SHADE_WALLS || DEPTH_SHADE_FLOOR || DEPTH_SHADE_CEILING || USE_SIDE_SHADING)
    register u8  base;
    register s16 shaded;
    register s16 below_base;
#endif /* DEPTH_SHADE_WALLS || DEPTH_SHADE_FLOOR || DEPTH_SHADE_CEILING || USE_SIDE_SHADING */

    if (p->is_floor) {
    
#if TEXTURED_FLOOR
        color = sampleTexture(floor_tex, p->texCoords.x, p->texCoords.y);
#elif COLORED_FLOOR
        register s16 tile_x = p->texCoords.x >> 10;
        register s16 tile_y = p->texCoords.y >> 10;
        color = floorColorMap[tile_y * LEVEL_W + tile_x];
#else
        color = PAL_FLOOR;
#endif /* TEXTURED_FLOORS */
#if DEPTH_SHADE_FLOOR
        /* Clamp color to within row */
/*
        base        = color & 0xF0;
        shaded      = color - p->shading;
        shaded     &= ~(shaded >> 15);               // clamp to 0 if negative
        below_base  = shaded - (s16)base;
        shaded     &= ~(below_base >> 15);           // clamp to 0 if crossed row boundary
//*/
        color       = darkLut[color][p->shading];//(u8)shaded;
#endif /* DEPTH_SHADE_FLOOR */

    }
    else {
        // Ceiling / sky

#if TEXTURED_CEILING
        color = sampleTexture(ceil_tex, p->texCoords.x, p->texCoords.y);
#else
        color = PAL_CEIL;
#endif /* TEXTURED_CEILING */
#if DEPTH_SHADE_CEILING
        /* Clamp color to within row */
/*
        base        = color & 0xF0;
        shaded      = color - p->shading;
        shaded     &= ~(shaded >> 15);               // clamp to 0 if negative
        below_base  = shaded - (s16)base;
        shaded     &= ~(below_base >> 15);           // clamp to 0 if crossed row boundary
//*/
        color       = darkLut[color][p->shading];//(u8)shaded;
#endif /* DEPTH_SHADE_CEILING */
    }

    *p->destination = (u16)color | ((u16)color << 8);
}


/*  Helper function that determines intersection with both ceiling and floor. 
*/
static inline
YGR_Unit 
_RENDER_floorCeilFunction(s16 x, s16 y)
{
    YGR_Unit f = _RENDER_floorFunction(x,y);

    if (_RENDER_ceilFunction == 0)
        return f;

    YGR_Unit c = _RENDER_ceilFunction(x,y);

  return ((f & 0x00ff) << 8) | (c & 0x00ff);
}

static inline
YGR_Unit 
_floorHeightNotZeroFunction(s16 x, s16 y)
{
    return (RENDER_RS_HEIGHT_FN(x,y) == 0) 
        ? 0 
        : MATH_nonZero((x & 0x00FF) | ((y & 0x00FF) << 8));
        // ^ this makes collisions between all squares - needed for rolling doors
}

static inline void
_RENDER_precomputeFlatsDepths(void)
{
    YGR_Unit 
        cam_height   = _RENDER_camera->height,
        dist_to_ceil = YGR_UNITS_PER_SQUARE * 2 - cam_height;

    s16 horizon = _RENDER_middleRow;
    u8 limit;
    
    if (0 <= horizon && horizon < SCREEN_H) {
        _RENDER_flatsDepthBuf[_RENDER_page][horizon] = YGR_INFINITY;
        limit = horizon;
    }
    else {
        limit = (horizon < 0)
            ? 0
            : SCREEN_H - 1;
    }

    for (u8 i = 0; i < limit; i++) {
        _RENDER_flatsDepthBuf[_RENDER_page][i] = MATH_fast_div(
                dist_to_ceil * (RENDER_VERTICAL_FOV), 
                MATH_nonZero(horizon - i)
            );
    }

    for (u8 i = limit + 1; i < _RENDER_camResYLimit + 1; i++) {
        _RENDER_flatsDepthBuf[_RENDER_page][i] = MATH_fast_div(
                cam_height   * (RENDER_VERTICAL_FOV), 
                MATH_nonZero(i - horizon)
            );
    }
#if DEPTH_SHADE_FLOOR || DEPTH_SHADE_CEILING
    for (u8 y = 0; y < _RENDER_camResYLimit + 1; y++) {
        register YGR_Unit depth = _RENDER_flatsDepthBuf[_RENDER_page][y] ;
        register u8       shade = ( depth >> DEPTH_SHIFT_AMOUNT >> 2 );
        if (
            _RENDER_flatsShadeBuf[_RENDER_page][y] != shade
//*
            && !(
                    _RENDER_wallTopMin[_RENDER_page] < y
                    && y < _RENDER_wallBotMin[_RENDER_page]
                )
//*/
        ) {
            RENDER_PixelInfo p;
            p.is_floor    = (y >= horizon);
            p.is_wall     = 0;
            p.shading     = MATH_clamp(shade, -16, 16);
            p.position.y  = y;
            p.destination = _RENDER_drawBuf + (y * (SCREEN_W >> 1));

            for (u8 x = 0; x < RENDER_W; x++) {
                p.position.x = x;
                _RENDER_flatsPixel(&p);
                p.destination++;
            }
        }

        _RENDER_flatsShadeBuf[_RENDER_page][y] = shade;
    }
#endif
}


/* Helper for drawing floor or ceiling. Returns the last drawn pixel position. 
*/
IWRAM_CODE
static 
s16 
_RENDER_drawFlatsColumn(
    YGR_Unit    yCurrent,
    YGR_Unit    yTo,
    YGR_Unit    limit1, // TODO: s16?
    YGR_Unit    limit2,
    YGR_Unit    verticalOffset,
    s16         increment,
    s8          computeDepth,
    s8          computeCoords,
    s16         depthIncrementMultiplier,
    YGR_Ray    *ray,
    RENDER_PixelInfo *pixelInfo
)
{
    _RENDER_UNUSED(ray);

    YGR_Vec2 cam_pos = _RENDER_camera->position;
    YGR_Unit rowStep = increment * (SCREEN_W >> 1);
    YGR_Unit depthIncrement;
    YGR_Unit dx;
    YGR_Unit dy;
    YGR_Unit d2;
    YGR_Unit rcp_d2;
    

    pixelInfo->is_wall = 0;

    s16 limit  = MATH_clamp(yTo,limit1,limit2);

    YGR_Unit depth = 0; /* TODO: this is for clamping depth to 0 so that we don't
                            have negative depths, but we should do it more
                            elegantly and efficiently */

    _RENDER_UNUSED(depth);

    /* for performance reasons have different version of the critical loop
        to be able to branch early */
    #define loop(doDepth,doCoords)\
        {\
            if (doDepth) { /*constant condition - compiler should optimize it out*/\
                depth = pixelInfo->depth + MATH_abs(verticalOffset) *\
                        RENDER_VERTICAL_DEPTH_MULTIPLY;\
                depthIncrement = depthIncrementMultiplier *\
                        _RENDER_horizontalDepthStep;\
            }\
            if (doCoords) { /*constant condition - compiler should optimize it out*/\
                dx     = pixelInfo->hit.position.x - cam_pos.x;\
                dy     = pixelInfo->hit.position.y - cam_pos.y;\
                d2     = MATH_nonZero(pixelInfo->hit.distance);\
                rcp_d2 = depth_reciprocal[MATH_min(d2, DEPTH_RECIPROCAL_SIZE - 1)]; \
            }\
            pixelInfo->destination = _RENDER_drawBuf \
                    + ((yCurrent + increment) \
                            * (SCREEN_W >> 1) \
                        ) \
                    + pixelInfo->position.x; \
            for ( \
                s16 i = yCurrent + increment;\
                increment == -1 ? i >= limit : i <= limit; /* TODO: is efficient? */\
                i += increment \
            ) {\
                pixelInfo->position.y = i;\
                if (doDepth) { /*constant condition - compiler should optimize it out*/\
                    depth += depthIncrement;\
                    pixelInfo->depth   = _RENDER_flatsDepthBuf[_RENDER_page][i]; \
                    /* ^ int comparison is fast, it is not braching! (= test instr.) */\
                }\
                if (doCoords) { /*constant condition - compiler should optimize it out*/\
                    YGR_Unit d = _RENDER_floorPixelDistances[i];\
                    pixelInfo->texCoords.x =\
                        cam_pos.x \
                        + (((s32)(d * dx) * rcp_d2) >> 10); \
                    pixelInfo->texCoords.y =\
                        cam_pos.y \
                        + (((s32)(d * dy) * rcp_d2) >> 10); \
                }\
                pixelInfo->shading = _RENDER_flatsShadeBuf[_RENDER_page][i]; \
                _RENDER_flatsPixel(pixelInfo);\
                pixelInfo->destination += rowStep; \
            }\
        }

    if (computeDepth) // branch early
    {
        if (!computeCoords)
        loop(1,0)
        else
        loop(1,1)
    }
    else
    {
        if (!computeCoords)
        loop(0,0)
        else
        loop(1,1)
    }

    #undef loop

    return limit;
}


/* Helper for drawing walls. Returns the last drawn pixel position. */
IWRAM_CODE
static inline 
s16 
_RENDER_drawWall(
    YGR_Unit yCurrent,
    YGR_Unit yFrom,
    YGR_Unit yTo,
    YGR_Unit limit1, // TODO: s16?
    YGR_Unit limit2,
    YGR_Unit height,
    s16 increment,
    RENDER_PixelInfo *pixelInfo
)
{
    _RENDER_UNUSED(height)

    height = MATH_abs(height);

    pixelInfo->is_wall = 1;

    YGR_Unit limit = MATH_clamp(yTo,limit1,limit2);

    YGR_Unit wallLength = MATH_nonZero(MATH_abs(yTo - yFrom - 1));

    YGR_Unit wallPosition = MATH_abs(yFrom - yCurrent) - increment;

    YGR_Unit heightScaled = height << RENDER_TEXTURE_INTERPOLATION_SHIFT;
    _RENDER_UNUSED(heightScaled);

    YGR_Unit coordStepScaled = RENDER_COMPUTE_WALL_TEXCOORDS ?
#if RENDER_TEXTURE_VERTICAL_STRETCH == 1
    ((YGR_UNITS_PER_SQUARE << RENDER_TEXTURE_INTERPOLATION_SHIFT) / wallLength)
#else
    (heightScaled / wallLength)
#endif
    : 0;

    pixelInfo->texCoords.y = RENDER_COMPUTE_WALL_TEXCOORDS ?
        (wallPosition * coordStepScaled) : 0;

    if (increment < 0)
    {
        coordStepScaled *= -1;
        pixelInfo->texCoords.y =
#if RENDER_TEXTURE_VERTICAL_STRETCH == 1
        (YGR_UNITS_PER_SQUARE << RENDER_TEXTURE_INTERPOLATION_SHIFT)
        - pixelInfo->texCoords.y;
#else
        heightScaled - pixelInfo->texCoords.y;
#endif
    }
  else
    {
        // with floor wall, don't start under 0
        pixelInfo->texCoords.y = MATH_zeroClamp(pixelInfo->texCoords.y);
    }

    YGR_Unit textureCoordScaled = pixelInfo->texCoords.y;
    YGR_Unit rowStep = increment * (SCREEN_W >> 1);
    pixelInfo->destination = _RENDER_drawBuf
            + ((yCurrent + increment) * (SCREEN_W >> 1))
            + pixelInfo->position.x;

    for (
        u8 i = yCurrent + increment; 
        increment == -1 ? i >= limit : i <= limit; // TODO: is efficient?
        i += increment
    ) {
        pixelInfo->position.y = i;

#if RENDER_COMPUTE_WALL_TEXCOORDS == 1
        pixelInfo->texCoords.y = textureCoordScaled >> RENDER_TEXTURE_INTERPOLATION_SHIFT;

        textureCoordScaled += coordStepScaled;
#endif

#if (DEPTH_SHADE_WALLS || USE_SIDE_SHADING)
    /*  BEGIN SHADE ASSIGNMENT */
        pixelInfo->shading = (s8)MATH_clamp( (
    #if USE_SIDE_SHADING == 1
                // Side-faces (N/S vs E/W) get a subtle brightness boost.
                1 - (pixelInfo->hit.direction & 1)
    #elif USE_SIDE_SHADING == 2 
                4 - (pixelInfo->hit.direction)
    #endif /* USE_SIDE_SHADING */
    #if (USE_SIDE_SHADING && DEPTH_SHADE_WALLS)
                +
    #endif /* USE_SIDE_SHADING && DEPTH_SHADE_WALLS */
    #if DEPTH_SHADE_WALLS
                (pixelInfo->depth >> DEPTH_SHIFT_AMOUNT)
    #endif /* DEPTH_SHADE_WALLS */
            ), -16, 16);
    /*  END SHADE ASSIGNMENT */
#endif /* DEPTH_SHADE_WALLS || USE_SIDE_SHADING */

        _RENDER_wallPixel(pixelInfo);
        pixelInfo->destination += rowStep;
    }

    return limit;
}

/// Fills a RENDER_HitResult struct with info for a hit at infinity.
IWRAM_CODE
static inline 
void 
_RENDER_makeInfiniteHit(RENDER_HitResult *hit, YGR_Ray *ray)
{
    hit->distance = YGR_UNITS_PER_SQUARE * YGR_UNITS_PER_SQUARE;
    /* ^ horizon is at infinity, but we can't use too big infinity
        (YGR_INFINITY) because it would overflow in the following mult. */
    hit->position.x = MATH_fast_div(
            (ray->direction.x * hit->distance), 
            YGR_UNITS_PER_SQUARE
        );
    hit->position.y = MATH_fast_div(
            (ray->direction.y * hit->distance), 
            YGR_UNITS_PER_SQUARE
        );

    hit->direction = 0;
    hit->texture_coord = 0;
    hit->array_value = 0;
    hit->doorRoll = 0;
    hit->type = 0;
}


IWRAM_CODE
static inline
void 
_RENDER_columnFunction(
    RENDER_HitResult *hits, 
    u16 hitCount,
    u16 x, 
    YGR_Ray ray
)
{
    YGR_Unit 
        y = 0,
    
        wallHeightScreen = 0,
        wallStart = _RENDER_middleRow,
        wallEnd   = _RENDER_middleRow,

        dist = 1;

    RENDER_PixelInfo p;
    p.position.x = x;
    p.wall_height = YGR_UNITS_PER_SQUARE;

    if (hitCount > 0) {
        RENDER_HitResult hit = hits[0];
        _RENDER_depthBuf[_RENDER_page][x] = hit.distance;

        u8 goOn = 1;

        if (_RENDER_rollFunction != 0 && RENDER_COMPUTE_WALL_TEXCOORDS == 1) {
            if (hit.array_value == 0) {
                // standing inside door square, looking out => move to the next hit

                if (hitCount > 1)
                    hit = hits[1];
                else
                    goOn = 0;
            }
            else {
                // normal hit, check the door roll

                YGR_Unit texCoordMod = MATH_fast_mod(hit.texture_coord, YGR_UNITS_PER_SQUARE);

                s8 unrolled = hit.doorRoll >= 0 ?
                (hit.doorRoll > texCoordMod) :
                (texCoordMod > YGR_UNITS_PER_SQUARE + hit.doorRoll);

                if (unrolled)
                {
                    goOn = 0;

                    if (hitCount > 1) { /* should probably always be true (hit on square
                                        exit) */
                        if (
                            MATH_fast_mod(hit.direction, 2) 
                            != MATH_fast_mod(hits[1].direction, 2)
                        ) {
                            // hit on the inner side
                            hit = hits[1];
                            goOn = 1;
                        }
                        else if (hitCount > 2) {
                            // hit on the opposite side
                            hit = hits[2];
                            goOn = 1;
                        }
                    }
                }
            }
        }

        p.hit = hit;

        if (goOn) {
            dist = hit.distance;

            YGR_Unit wallHeightWorld = _RENDER_floorFunction(hit.square.x,hit.square.y);

            if (wallHeightWorld < 0) {
                /* We can't just do wallHeightWorld = max(0,wallHeightWorld) because
                we would be processing an actual hit with height 0, which shouldn't
                ever happen, so we assign some arbitrary height. */
                wallHeightWorld = YGR_UNITS_PER_SQUARE;
            }

            YGR_Unit worldPointTop = wallHeightWorld - _RENDER_camera->height;
            YGR_Unit worldPointBottom = -1 * _RENDER_camera->height;

            wallStart = _RENDER_middleRow - MATH_fast_div(
                    (
                            RENDER_perspectiveScaleVertical(worldPointTop,dist)
                            * _RENDER_camera->resolution.y
                        ),
                    YGR_UNITS_PER_SQUARE
                );

            wallEnd =  _RENDER_middleRow - MATH_fast_div(
                    (
                            RENDER_perspectiveScaleVertical(worldPointBottom,dist)
                            * _RENDER_camera->resolution.y
                        ), 
                    YGR_UNITS_PER_SQUARE
                );

            wallHeightScreen = wallEnd - wallStart;

            if (wallHeightScreen <= 0) // can happen because of rounding errors
                wallHeightScreen = 1; 
        }
        else {
            wallStart        = 0;
            wallEnd          = _RENDER_camResYLimit;
            wallHeightScreen = 0;
        }
    }
    else {
        _RENDER_makeInfiniteHit(&p.hit,&ray);
        _RENDER_depthBuf[_RENDER_page][x] = YGR_INFINITY;
        wallStart        = 0;
        wallEnd          = _RENDER_camResYLimit;
        wallHeightScreen = 0;
            
        _RENDER_wallTopBuf[_RENDER_page][x] = MATH_clamp(wallStart, 0, _RENDER_camResYLimit);
        _RENDER_wallBotBuf[_RENDER_page][x] = MATH_clamp(wallEnd,   0, _RENDER_camResYLimit);
    }
    
    /* Draw ceiling */
    p.is_wall = 0;
    p.is_floor = 0;
    p.is_horizon = 1;
#if RENDER_COMPUTE_CEILING_DEPTH == 1
    
#else
    p.depth = 1;
#endif /* RENDER_COMPUTE_CEILING_DEPTH */
    p.height = YGR_UNITS_PER_SQUARE;
    
    u8 prev_top = MATH_clamp(_RENDER_wallTopBuf[_RENDER_page][x], 0, SCREEN_H);
    YGR_Unit clampedWallStart = MATH_clamp(wallStart, 0, _RENDER_camResYLimit);
    YGR_Unit clampedWallEnd   = MATH_clamp(wallEnd,   0, _RENDER_camResYLimit);

    if (prev_top <= clampedWallStart) {
#if RENDER_COMPUTE_CEILING_DEPTH == 1
        p.depth = _RENDER_horizontalDepthStep;
#endif
        y = MATH_clamp(
                _RENDER_drawFlatsColumn(
                        prev_top,
                        clampedWallStart,
                        -1,
                        clampedWallStart,
                        _RENDER_camera->height,
                        1,
                        RENDER_COMPUTE_CEILING_DEPTH,
                        0,
                        1,
                        &ray,
                        &p
                    ),
                0,
                _RENDER_camResYLimit
            );
    }
    else
        y = clampedWallStart;

    /* DRAW WALL */
    p.is_wall = 1;
    p.is_floor = 1;
    p.depth = dist;
    p.height = 0;

#if RENDER_ROLL_TEXTURE_COORDS == 1 && RENDER_COMPUTE_WALL_TEXCOORDS == 1 
    p.hit.texture_coord -= p.hit.doorRoll;
#endif /* RENDER_ROLL_TEXTURE_COORDS == 1 && RENDER_COMPUTE_WALL_TEXCOORDS == 1  */

    p.texCoords.x = p.hit.texture_coord;
    p.texCoords.y = 0;

    YGR_Unit limit = _RENDER_drawWall(
            y,
            wallStart,
            wallEnd,
            -1,
            _RENDER_camResYLimit,
            p.hit.array_value,
            1,
            &p
        );

    y = MATH_max(y,limit); // take max, in case no wall was drawn
    y = MATH_max(y,wallStart);

    /* DRAW FLOOR */
    p.is_wall = 0;
#if RENDER_COMPUTE_FLOOR_DEPTH == 1
#else
    p.depth = 1;
#endif /* RENDER_COMPUTE_FLOOR_DEPTH */
    
    u8 prev_bot = _RENDER_wallBotBuf[_RENDER_page][x];
    if (clampedWallEnd < prev_bot)
#if RENDER_COMPUTE_FLOOR_DEPTH == 1
        p.depth = (_RENDER_camera->resolution.y - y) * _RENDER_horizontalDepthStep + 1;
#endif
        _RENDER_drawFlatsColumn(
                clampedWallEnd,
                prev_bot,
                -1,
                prev_bot,
                _RENDER_camera->height,
                1,
                RENDER_COMPUTE_FLOOR_DEPTH,
                RENDER_COMPUTE_FLOOR_TEXCOORDS,
                -1,
                &ray,
                &p
            );
    
    _RENDER_wallTopBuf[_RENDER_page][x] = clampedWallStart;
    _RENDER_wallBotBuf[_RENDER_page][x] = clampedWallEnd;
//*
    if      (clampedWallStart < _RENDER_wallTopMin[_RENDER_page])
        _RENDER_wallTopMin[_RENDER_page] = clampedWallStart;
    else if (_RENDER_wallBotMin[_RENDER_page] < clampedWallEnd)
        _RENDER_wallBotMin[_RENDER_page] = clampedWallEnd;
//*/
}

/*
  Precomputes a distance from camera to the floor at each screen row into an
  array (must be preallocated with sufficient (camera.resolution.y) length).
*/
IWRAM_CODE 
static inline 
void _RENDER_precomputeFloorDistances(
    YGR_Unit *dest, 
    u16 startIndex
)
{
    YGR_Unit cam_res_y = _RENDER_camera->resolution.y;
    YGR_Unit camHeightScreenSize =  MATH_fast_div(
            (_RENDER_camera->height * cam_res_y), 
            YGR_UNITS_PER_SQUARE
        );

    for (u16 i = startIndex; i < cam_res_y; ++i)
        dest[i] = RENDER_perspectiveScaleVerticalInverse(
                camHeightScreenSize,
                MATH_abs(i - _RENDER_middleRow)
            );
}

/*
  Ugly temporary hack to solve mapping to screen. This function computes
  (approximately, usin a table) a divisor needed for FOV correction.
*/
IWRAM_CODE 
static inline 
YGR_Unit 
_RENDER_fovCorrectionFactor(YGR_Unit fov)
{
  u16 table[9] = 
    {1,208,408,692,1024,1540,2304,5376,30000};

  fov = MATH_min((YGR_UNITS_PER_SQUARE >> 1) - 1,fov);

  u8  index = fov >> 6;
  u32 t     = ((fov - index * 64) * YGR_UNITS_PER_SQUARE) >> 6; 
  u32 v1    = table[index];
  u32 v2    = table[index + 1];
 
  return v1 + MATH_fast_div(((v2 - v1) * t), YGR_UNITS_PER_SQUARE);
}


/*  Casts rays for given camera view and for each hit calls a user provided
    function.
*/
IWRAM_CODE 
static inline
void 
_RENDER_castRaysMultiHit(
    RENDER_ArrayFunction   arrayFunc,
    RENDER_ArrayFunction   typeFunction, 
    RENDER_ColumnFunction  columnFunc,
    YGR_RayConstraints     constraints
)
{
    YGR_Unit angle      = _RENDER_camera->angle;
    YGR_Vec2 resolution = _RENDER_camera->resolution;
    
    YGR_Vec2 dir1 = MATH_angleToDirection(
            angle - RENDER_HORIZONTAL_FOV_HALF
        );

    YGR_Vec2 dir2 = MATH_angleToDirection(
            angle + RENDER_HORIZONTAL_FOV_HALF
        );

    /* We scale the side distances so that the middle one is
        YGR_UNITS_PER_SQUARE, which has to be this way. */
    YGR_Unit cos = MATH_nonZero(MATH_cos(RENDER_HORIZONTAL_FOV_HALF));

    dir1.x = MATH_fast_div((dir1.x * YGR_UNITS_PER_SQUARE), cos);
    dir1.y = MATH_fast_div((dir1.y * YGR_UNITS_PER_SQUARE), cos);

    dir2.x = MATH_fast_div((dir2.x * YGR_UNITS_PER_SQUARE), cos);
    dir2.y = MATH_fast_div((dir2.y * YGR_UNITS_PER_SQUARE), cos);

    YGR_Unit dX = dir2.x - dir1.x;
    YGR_Unit dY = dir2.y - dir1.y;

    RENDER_HitResult hits[constraints.max_hits];
    u16 hitCount;

    YGR_Ray r;
    r.start = _RENDER_camera->position;

    YGR_Unit currentDX = 0;
    YGR_Unit currentDY = 0;

    for (s16 i = 0; i < resolution.x; ++i) {
        /* Here by linearly interpolating the direction vector its length changes,
        which in result achieves correcting the fish eye effect (computing
        perpendicular distance). */

        r.direction.x = dir1.x + MATH_fast_div(currentDX, resolution.x);
        r.direction.y = dir1.y + MATH_fast_div(currentDY, resolution.x);

        RENDER_castRayMultiHit(r,arrayFunc,typeFunction,hits,&hitCount,constraints);

        columnFunc(hits,hitCount,i,r);

        currentDX += dX;
        currentDY += dY;
    }
}


/*******************************************************************************
    PUBLIC METHODS
*******************************************************************************/
IWRAM_CODE 
void 
RENDER_plot(u8 x, u8 y, u8 clr)
{
    // Address of the halfword that contains pixel (x, y)
    register u16 *p = _RENDER_drawBuf + ((y * SCREEN_W + x) >> 1);
 
    if (x & 1)
        // odd column → high byte
        *p = (*p & 0x00FF) | ((u16)clr << 8);
    else
        // even column → low byte
        *p = (*p & 0xFF00) | clr;
}

IWRAM_CODE 
RENDER_PixelInfo 
RENDER_mapToScreen(
    YGR_Vec2 worldPosition, 
    YGR_Unit     height,
    YGR_Camera   camera
)
{
    RENDER_PixelInfo result;

    YGR_Vec2 toPoint;

    toPoint.x = worldPosition.x - camera.position.x;
    toPoint.y = worldPosition.y - camera.position.y;

    YGR_Unit middleColumn = camera.resolution.x >> 1;

    // rotate the point to camera space (y left/right, x forw/backw)

    YGR_Unit cos = MATH_cos(camera.angle);
    YGR_Unit sin = MATH_sin(camera.angle);

    YGR_Unit tmp = toPoint.x;

    toPoint.x = MATH_fast_div((toPoint.x * cos - toPoint.y * sin), YGR_UNITS_PER_SQUARE); 
    toPoint.y = MATH_fast_div((tmp * sin + toPoint.y * cos), YGR_UNITS_PER_SQUARE); 

    result.depth = toPoint.x;

    result.position.x = middleColumn - MATH_fast_div(
            (
                    RENDER_perspectiveScaleHorizontal(toPoint.y,result.depth) 
                    * middleColumn
                ),
            YGR_UNITS_PER_SQUARE
        );

    result.position.y = MATH_fast_div(
            (
                    RENDER_perspectiveScaleVertical(height - camera.height,result.depth)
                    * camera.resolution.y
                ), 
            YGR_UNITS_PER_SQUARE
        );
    
    result.position.y = (camera.resolution.y >> 1) - result.position.y + camera.shear;

    return result;
}


IWRAM_CODE
void 
RENDER_castRayMultiHit(
    YGR_Ray ray, 
    RENDER_ArrayFunction arrayFunc,
    RENDER_ArrayFunction typeFunc, 
    RENDER_HitResult *hitResults,
    u16 *hitResultsLen, 
    YGR_RayConstraints constraints
)
{
    YGR_Vec2 currentPos = ray.start;
    YGR_Vec2 currentSquare;

    currentSquare.x = MATH_divRoundDown(ray.start.x,YGR_UNITS_PER_SQUARE);
    currentSquare.y = MATH_divRoundDown(ray.start.y,YGR_UNITS_PER_SQUARE);

    *hitResultsLen = 0;

    YGR_Unit squareType = arrayFunc(currentSquare.x,currentSquare.y);

    // DDA variables
    YGR_Vec2 nextSideDist; // dist. from start to the next side in given axis
    YGR_Vec2 delta;
    YGR_Vec2 step;         // -1 or 1 for each axis
    s8 stepHorizontal = 0; // whether the last step was hor. or vert.

    nextSideDist.x = 0;
    nextSideDist.y = 0;

    YGR_Unit dirVecLengthNorm = MATH_len(ray.direction) * YGR_UNITS_PER_SQUARE;

    /* KEEP AS REGULAR DIVISIONS */
    delta.x = MATH_abs(dirVecLengthNorm / MATH_nonZero(ray.direction.x));
    delta.y = MATH_abs(dirVecLengthNorm / MATH_nonZero(ray.direction.y));

    // init DDA

    if (ray.direction.x < 0) {
        step.x = -1;
        nextSideDist.x = MATH_fast_div(
                (MATH_wrap(ray.start.x,YGR_UNITS_PER_SQUARE) * delta.x),
                YGR_UNITS_PER_SQUARE
            );
    }
    else {
        step.x = 1;
        nextSideDist.x = MATH_fast_div(
                ( (
                    MATH_wrap(
                            YGR_UNITS_PER_SQUARE 
                            - ray.start.x,YGR_UNITS_PER_SQUARE
                        ) ) * delta.x
                    ),
                YGR_UNITS_PER_SQUARE
            );
    }

    if (ray.direction.y < 0)
    {
        step.y = -1;
        nextSideDist.y = MATH_fast_div(
                (MATH_wrap(ray.start.y,YGR_UNITS_PER_SQUARE) * delta.y),
                YGR_UNITS_PER_SQUARE
            );
    }
    else
    {
        step.y = 1;
        nextSideDist.y = MATH_fast_div(
                ( (
                    MATH_wrap(
                            YGR_UNITS_PER_SQUARE - ray.start.y,
                            YGR_UNITS_PER_SQUARE
                        )) * delta.y
                    ),
                YGR_UNITS_PER_SQUARE
            );
    }

    // DDA loop

    #define RECIP_SCALE 65536

    YGR_Unit dx = MATH_nonZero(ray.direction.x);
    YGR_Unit dy = MATH_nonZero(ray.direction.y);
    YGR_Unit rayDirXRecip = depth_reciprocal_65536[MATH_abs(dx)]; if (dx < 0) rayDirXRecip = -rayDirXRecip;
    YGR_Unit rayDirYRecip = depth_reciprocal_65536[MATH_abs(dy)]; if (dy < 0) rayDirYRecip = -rayDirYRecip;
    // ^ we precompute reciprocals to avoid divisions in the loop

    for (u16 i = 0; i < constraints.max_steps; ++i) {
        YGR_Unit currentType = RENDER_RS_HEIGHT_FN(currentSquare.x,currentSquare.y);

        if (MATH_unlikely(currentType != squareType)) {
            // collision
            RENDER_HitResult h;

            h.array_value = currentType;
            h.doorRoll = 0;
            h.position = currentPos;
            h.square   = currentSquare;

            if (stepHorizontal) {
                h.position.x = currentSquare.x * YGR_UNITS_PER_SQUARE;
                h.direction = 3;

                if (step.x == -1)
                {
                h.direction = 1;
                h.position.x += YGR_UNITS_PER_SQUARE;
                }

                YGR_Unit diff = h.position.x - ray.start.x;

                h.position.y = // avoid division by multiplying with reciprocal
                    ray.start.y 
                    + MATH_fast_div(
                            (ray.direction.y * diff * rayDirXRecip),
                            RECIP_SCALE
                        );

#if RENDER_RECTILINEAR
    /* Here we compute the fish eye corrected distance (perpendicular to
    the projection plane) as the Euclidean distance (of hit from camera
    position) divided by the length of the ray direction vector. This can
    be computed without actually computing Euclidean distances as a
    hypothenuse A (distance) divided by hypothenuse B (length) is equal to
    leg A (distance along principal axis) divided by leg B (length along
    the same principal axis). */

#define CORRECT(dir1,dir2)\
    YGR_Unit tmp = diff >> 2;        /* 4 to prevent overflow */ \
    h.distance = ((tmp >> 3) != 0) /* prevent a bug with small dists */ \
        ? MATH_fast_div((tmp * YGR_UNITS_PER_SQUARE * rayDir ## dir1 ## Recip), (RECIP_SCALE >> 2))\
        : MATH_abs(h.position.dir2 - ray.start.dir2)

                CORRECT(X,y);

#endif // RENDER_RECTILINEAR
            }
            else {
                h.position.y = currentSquare.y * YGR_UNITS_PER_SQUARE;
                h.direction = 2;

                if (step.y == -1) {
                h.direction = 0;
                h.position.y += YGR_UNITS_PER_SQUARE;
                }

                YGR_Unit diff = h.position.y - ray.start.y;

                h.position.x =
                    ray.start.x 
                    + MATH_fast_div(
                            (ray.direction.x * diff * rayDirYRecip),
                            RECIP_SCALE
                        );

#if RENDER_RECTILINEAR

                CORRECT(Y,x); // same as above but for different axis

#undef CORRECT

#endif // RENDER_RECTILINEAR
            }

#if !RENDER_RECTILINEAR
            h.distance = MATH_dist(h.position, ray.start);
#endif
            if (typeFunc != 0)
                h.type = typeFunc(currentSquare.x,currentSquare.y);

#if RENDER_COMPUTE_WALL_TEXCOORDS == 1
            switch (h.direction) {
            case 0: h.texture_coord =
                MATH_wrap(-1 * h.position.x,YGR_UNITS_PER_SQUARE); break;

            case 1: h.texture_coord =
                MATH_wrap(h.position.y,YGR_UNITS_PER_SQUARE); break;

            case 2: h.texture_coord =
                MATH_wrap(h.position.x,YGR_UNITS_PER_SQUARE); break;

            case 3: h.texture_coord =
                MATH_wrap(-1 * h.position.y,YGR_UNITS_PER_SQUARE); break;

            default: h.texture_coord = 0; break;
            }

            if (_RENDER_rollFunction != 0) {
                h.doorRoll = _RENDER_rollFunction(currentSquare.x,currentSquare.y);
                
                if (h.direction == 0 || h.direction == 1)
                    h.doorRoll *= -1;
            }

#else
            h.texture_coord = 0;
#endif

            hitResults[*hitResultsLen] = h;

            *hitResultsLen += 1;

            squareType = currentType;

            if (*hitResultsLen >= constraints.max_hits)
                break;
        }

        // DDA step

        if (nextSideDist.x < nextSideDist.y) {
            nextSideDist.x += delta.x;
            currentSquare.x += step.x;
            stepHorizontal = 1;
        }
        else {
            nextSideDist.y += delta.y;
            currentSquare.y += step.y;
            stepHorizontal = 0;
        }
    }
}


IWRAM_CODE
RENDER_HitResult 
RENDER_castRay(YGR_Ray ray, RENDER_ArrayFunction arrayFunc)
{
    RENDER_HitResult result;
    u16 len;
    YGR_RayConstraints c;

    c.max_steps = 1000;
    c.max_hits = 1;

    RENDER_castRayMultiHit(ray,arrayFunc,0,&result,&len,c);

    if (len == 0)
        result.distance = -1;

    return result;
}


IWRAM_CODE 
void 
RENDER_renderSimple(
    YGR_Camera           *cam, 
    RENDER_ArrayFunction  floorHeightFunc,
    RENDER_ArrayFunction  typeFunc, 
    RENDER_ArrayFunction  rollFunc,
    YGR_RayConstraints    constraints
)
{
    YGR_Vec2 cam_res = cam->resolution;
    _RENDER_floorFunction            = floorHeightFunc;
    _RENDER_camera                   = cam;
    _RENDER_camResYLimit             = cam_res.y - 1;
    _RENDER_middleRow                = (cam_res.y >> 1) + cam->shear;
    _RENDER_rollFunction             = rollFunc;
    _RENDER_wallTopMin[_RENDER_page] = SCREEN_H;
    _RENDER_wallBotMin[_RENDER_page] = 0;

    _RENDER_cameraHeightScreen = MATH_fast_div(
            (
                    _RENDER_camera->resolution.y 
                    * (_RENDER_camera->height - YGR_UNITS_PER_SQUARE)
                ),
            YGR_UNITS_PER_SQUARE
        );

    _RENDER_horizontalDepthStep = MATH_fast_div(RENDER_HORIZON_DEPTH, cam_res.y); 

    constraints.max_hits = (_RENDER_rollFunction == 0)
        ? 1  // no door => 1 hit is enough 
        : 3; // for correctly rendering rolling doors we'll need 3 hits (NOT 2)

    #if RENDER_COMPUTE_FLOOR_TEXCOORDS == 1
    YGR_Unit floorPixelDistances[cam_res.y];
    _RENDER_precomputeFloorDistances(floorPixelDistances,_RENDER_middleRow);
    _RENDER_floorPixelDistances = floorPixelDistances; // pass to column function
    #endif

    if (_RENDER_fovCorrectionFactors[1] == 0)
        _RENDER_fovCorrectionFactors[1] = _RENDER_fovCorrectionFactor(RENDER_VERTICAL_FOV);

    _RENDER_fovScale = MATH_fast_div(
        _RENDER_fovCorrectionFactors[1],
        YGR_UNITS_PER_SQUARE
    );
    
    _RENDER_precomputeFlatsDepths();
    
    _RENDER_castRaysMultiHit(
            _floorHeightNotZeroFunction,
            typeFunc,
            _RENDER_columnFunction, 
            constraints
        );
        
#if RENDER_COMPUTE_FLOOR_TEXCOORDS == 1
    _RENDER_floorPixelDistances = 0;
#endif

    _RENDER_drawSprites();
}

IWRAM_CODE 
YGR_Unit 
RENDER_perspectiveScaleVertical(YGR_Unit originalSize, YGR_Unit distance)
{
  if (_RENDER_fovCorrectionFactors[1] == 0)
    _RENDER_fovCorrectionFactors[1] = _RENDER_fovCorrectionFactor(RENDER_VERTICAL_FOV);

  return (distance != 0)
        ? MATH_fast_div(
                (originalSize * YGR_UNITS_PER_SQUARE),
                MATH_nonZero(
                        MATH_fast_div(
                                (_RENDER_fovCorrectionFactors[1] * distance), 
                                YGR_UNITS_PER_SQUARE
                            )
                    )
            ) 
        : 0;
}


IWRAM_CODE 
YGR_Unit 
RENDER_perspectiveScaleVerticalInverse(YGR_Unit originalSize, YGR_Unit scaledSize)
{
  if (_RENDER_fovCorrectionFactors[1] == 0)
    _RENDER_fovCorrectionFactors[1] = _RENDER_fovCorrectionFactor(RENDER_VERTICAL_FOV);

  return (scaledSize != 0)
        ? MATH_fast_div(
                (originalSize * YGR_UNITS_PER_SQUARE),
                MATH_nonZero( 
                        MATH_fast_div(
                                (_RENDER_fovCorrectionFactors[1] * scaledSize), 
                                YGR_UNITS_PER_SQUARE
                            )
                    )
            ) 
        : YGR_INFINITY;
}

IWRAM_CODE 
YGR_Unit
RENDER_perspectiveScaleHorizontal(YGR_Unit originalSize, YGR_Unit distance)
{
    if (_RENDER_fovCorrectionFactors[0] == 0)
        _RENDER_fovCorrectionFactors[0] = _RENDER_fovCorrectionFactor(RENDER_HORIZONTAL_FOV);

    return (distance != 0) 
        ? MATH_fast_div(
                (originalSize * YGR_UNITS_PER_SQUARE),
                MATH_nonZero(
                        MATH_fast_div(
                                (_RENDER_fovCorrectionFactors[0] * distance), 
                                YGR_UNITS_PER_SQUARE
                            )
                    )
            )
        : 0;
}

IWRAM_CODE 
YGR_Unit 
RENDER_perspectiveScaleHorizontalInverse(
    YGR_Unit originalSize,
    YGR_Unit scaledSize
)
{
  // TODO: probably doesn't work

  return scaledSize != 0 
    ? MATH_fast_div(
            (originalSize * YGR_UNITS_PER_SQUARE + (YGR_UNITS_PER_SQUARE >> 1)),
            MATH_fast_div(
                    (RENDER_HORIZONTAL_FOV_TAN * 2 * scaledSize), 
                    YGR_UNITS_PER_SQUARE
                )
        )       
    : YGR_INFINITY;
}

IWRAM_CODE 
YGR_Unit 
RENDER_castRay3D(
    YGR_Vec2       pos1, 
    YGR_Unit           height1, 
    YGR_Vec2       pos2, 
    YGR_Unit           height2,
    RENDER_ArrayFunction  floorHeightFunc, 
    RENDER_ArrayFunction  ceilingHeightFunc,
    YGR_RayConstraints constraints
)
{
    RENDER_HitResult hits[constraints.max_hits];
    u16 numHits;

    YGR_Ray ray;

    ray.start = pos1;

    YGR_Unit distance;

    ray.direction.x = pos2.x - pos1.x;
    ray.direction.y = pos2.y - pos1.y;

    distance = MATH_len(ray.direction);

    ray.direction = MATH_normalize(ray.direction); 

    YGR_Unit heightDiff = height2 - height1;

    RENDER_castRayMultiHit(ray,floorHeightFunc,0,hits,&numHits,constraints);

    YGR_Unit result = YGR_UNITS_PER_SQUARE;

    s16 squareX = MATH_divRoundDown(pos1.x,YGR_UNITS_PER_SQUARE);
    s16 squareY = MATH_divRoundDown(pos1.y,YGR_UNITS_PER_SQUARE);

    YGR_Unit startHeight = floorHeightFunc(squareX,squareY);

    #define checkHits(comp,res) \
    { \
        YGR_Unit currentHeight = startHeight; \
        for (u16 i = 0; i < numHits; ++i) \
        { \
        if (hits[i].distance > distance) \
            break;\
        YGR_Unit h = hits[i].array_value; \
        if ((currentHeight comp h ? currentHeight : h) \
            comp (height1 +  MATH_fast_div((hits[i].distance * heightDiff), distance))) \
        { \
            res =  MATH_fast_div((hits[i].distance * YGR_UNITS_PER_SQUARE), distance); \
            break; \
        } \
        currentHeight = h; \
        } \
    }

    checkHits(>,result)

    if (ceilingHeightFunc != 0)
    {
        YGR_Unit result2 = YGR_UNITS_PER_SQUARE;
    
        startHeight = ceilingHeightFunc(squareX,squareY);

        RENDER_castRayMultiHit(ray,ceilingHeightFunc,0,hits,&numHits,constraints);

        checkHits(<,result2)

        if (result2 < result)
        result = result2;
    }

    #undef checkHits

    return result;
}


IWRAM_CODE 
void 
RENDER_moveCameraWithCollision(
    YGR_Camera *camera, 
    YGR_Vec2 planeOffset,
    YGR_Unit heightOffset, 
    RENDER_ArrayFunction floorHeightFunc,
    RENDER_ArrayFunction ceilingHeightFunc, 
    s8 computeHeight, 
    s8 force
)
{
    s8 movesInPlane = planeOffset.x != 0 || planeOffset.y != 0;

    if (movesInPlane || force) {
        s16 xSquareNew, ySquareNew;

        YGR_Vec2 corner; // BBox corner in the movement direction
        YGR_Vec2 cornerNew;

        s16 xDir = planeOffset.x > 0 ? 1 : -1;
        s16 yDir = planeOffset.y > 0 ? 1 : -1;

        corner.x = camera->position.x + xDir * RENDER_CAMERA_COLL_RADIUS;
        corner.y = camera->position.y + yDir * RENDER_CAMERA_COLL_RADIUS;

        s16 xSquare = MATH_divRoundDown(corner.x,YGR_UNITS_PER_SQUARE);
        s16 ySquare = MATH_divRoundDown(corner.y,YGR_UNITS_PER_SQUARE);

        cornerNew.x = corner.x + planeOffset.x;
        cornerNew.y = corner.y + planeOffset.y;

        xSquareNew = MATH_divRoundDown(cornerNew.x,YGR_UNITS_PER_SQUARE);
        ySquareNew = MATH_divRoundDown(cornerNew.y,YGR_UNITS_PER_SQUARE);

        YGR_Unit bottomLimit = -1 * YGR_INFINITY;
        YGR_Unit topLimit = YGR_INFINITY;

        YGR_Unit currCeilHeight = YGR_INFINITY;

        if (computeHeight) {
            bottomLimit = camera->height 
                    - RENDER_CAMERA_COLL_HEIGHT_BELOW 
                    + RENDER_CAMERA_COLL_STEP_HEIGHT;

        topLimit = camera->height + RENDER_CAMERA_COLL_HEIGHT_ABOVE;

        if (ceilingHeightFunc != 0)
            currCeilHeight = ceilingHeightFunc(xSquare,ySquare);
        }

// checks a single square for collision against the camera
#define collCheck(dir,s1,s2)\
    if (computeHeight) { \
        YGR_Unit height = floorHeightFunc(s1,s2);\
        if ( \
            height > bottomLimit \
            || currCeilHeight - height \
            <  RENDER_CAMERA_COLL_HEIGHT_BELOW + RENDER_CAMERA_COLL_HEIGHT_ABOVE \
        )\
            dir##Collides = 1;\
        else if (ceilingHeightFunc != 0) { \
            YGR_Unit height2 = ceilingHeightFunc(s1,s2); \
            if ((height2 < topLimit) || ((height2 - height) < \
            (RENDER_CAMERA_COLL_HEIGHT_ABOVE + RENDER_CAMERA_COLL_HEIGHT_BELOW))) \
            dir##Collides = 1; \
        } \
    } \
    else \
        dir##Collides = floorHeightFunc(s1,s2) > RENDER_CAMERA_COLL_STEP_HEIGHT;

// check collision against non-diagonal square
#define collCheckOrtho(dir,dir2,s1,s2,x) \
    if (dir##SquareNew != dir##Square) { \
        collCheck(dir,s1,s2)\
    } \
    if (!dir##Collides) { /* now also check for coll on the neighbouring square */ \
        s16 dir2##Square2 = MATH_divRoundDown( \
                corner.dir2 - dir2##Dir \
                * RENDER_CAMERA_COLL_RADIUS * 2,YGR_UNITS_PER_SQUARE \
            );\
        if (dir2##Square2 != dir2##Square) {\
            if (x) \
                collCheck(dir,dir##SquareNew,dir2##Square2)\
            else \
                collCheck(dir,dir2##Square2,dir##SquareNew)\
        }\
    }

        s8 xCollides = 0;
        collCheckOrtho(x,y,xSquareNew,ySquare,1)

        s8 yCollides = 0;
        collCheckOrtho(y,x,xSquare,ySquareNew,0)

        if (xCollides || yCollides) {
            if (movesInPlane) {
                #define collHandle(dir)\
                if (dir##Collides)\
                cornerNew.dir = (dir##Square) * YGR_UNITS_PER_SQUARE \
                        + (YGR_UNITS_PER_SQUARE >> 1) + dir##Dir \
                        * (YGR_UNITS_PER_SQUARE >> 1) - dir##Dir \

                collHandle(x);
                collHandle(y);
            
                #undef collHandle
            }
            else {
                /* Player collides without moving in the plane; this can happen e.g. on
                elevators due to vertical only movement. This code can get executed
                when force == 1. */

                YGR_Vec2 squarePos;
                YGR_Vec2 newPos;

                squarePos.x = xSquare * YGR_UNITS_PER_SQUARE;
                squarePos.y = ySquare * YGR_UNITS_PER_SQUARE;

                newPos.x = MATH_max(
                        squarePos.x + RENDER_CAMERA_COLL_RADIUS + 1,
                        MATH_min(
                                squarePos.x + YGR_UNITS_PER_SQUARE 
                                        - RENDER_CAMERA_COLL_RADIUS - 1,
                                camera->position.x
                            )
                    );

                newPos.y = MATH_max(
                        squarePos.y + RENDER_CAMERA_COLL_RADIUS + 1,
                        MATH_min(
                                squarePos.y + YGR_UNITS_PER_SQUARE 
                                        - RENDER_CAMERA_COLL_RADIUS - 1,
                                camera->position.y
                            )
                    );

                cornerNew.x = corner.x + (newPos.x - camera->position.x);
                cornerNew.y = corner.y + (newPos.y - camera->position.y);
            }
        }
        else 
        {
        /* If no non-diagonal collision is detected, a diagonal/corner collision
            can still happen, check it here. */

        if (xSquare != xSquareNew && ySquare != ySquareNew)
        {
            s8 xyCollides = 0;
            collCheck(xy,xSquareNew,ySquareNew)
            
            if (xyCollides)
            {
            // normally should slide, but let's KISS and simply stop any movement
            cornerNew = corner;
            }
        }
        }

        #undef collCheck

        camera->position.x = cornerNew.x - xDir * RENDER_CAMERA_COLL_RADIUS;
        camera->position.y = cornerNew.y - yDir * RENDER_CAMERA_COLL_RADIUS;  
    }

    if (computeHeight && (movesInPlane || (heightOffset != 0) || force))
    {
        camera->height += heightOffset;

        s16 xSquare1 = MATH_divRoundDown(
                camera->position.x - RENDER_CAMERA_COLL_RADIUS,
                YGR_UNITS_PER_SQUARE
            );

        s16 xSquare2 = MATH_divRoundDown(
                camera->position.x + RENDER_CAMERA_COLL_RADIUS,
                YGR_UNITS_PER_SQUARE
            );

        s16 ySquare1 = MATH_divRoundDown(
                camera->position.y - RENDER_CAMERA_COLL_RADIUS,
                YGR_UNITS_PER_SQUARE
            );

        s16 ySquare2 = MATH_divRoundDown(
                camera->position.y + RENDER_CAMERA_COLL_RADIUS,
                YGR_UNITS_PER_SQUARE
            );

        YGR_Unit bottomLimit = floorHeightFunc(xSquare1,ySquare1);
        YGR_Unit topLimit    = (ceilingHeightFunc != 0) 
                ? ceilingHeightFunc(xSquare1,ySquare1) 
                : YGR_INFINITY;

        YGR_Unit height;

#define checkSquares(s1,s2)\
    { \
        height      = floorHeightFunc(xSquare##s1,ySquare##s2); \
        bottomLimit = MATH_max(bottomLimit,height); \
        height      = (ceilingHeightFunc != 0)  \
                ? ceilingHeightFunc(xSquare##s1,ySquare##s2)  \
                : YGR_INFINITY;\
        topLimit    = MATH_min(topLimit,height); \
    }

        if (xSquare2 != xSquare1)
            checkSquares(2,1)

        if (ySquare2 != ySquare1)
            checkSquares(1,2)

        if (xSquare2 != xSquare1 && ySquare2 != ySquare1)
            checkSquares(2,2)

        camera->height = MATH_clamp(
                camera->height,
                bottomLimit + RENDER_CAMERA_COLL_HEIGHT_BELOW,
                topLimit - RENDER_CAMERA_COLL_HEIGHT_ABOVE
            );

        #undef checkSquares
    }
}

IWRAM_CODE
void
RENDER_flip(void)
{
    VBlankIntrWait();
    
    _RENDER_drawBuf = (_RENDER_page == 0) 
        ? (u16*)MEM_VRAM 
        : (u16*)(MEM_VRAM + 0xA000);
    
    REG_DISPCNT  ^= DCNT_PAGE;
    _RENDER_page ^= 1;

    _RENDER_frame++;
}

IWRAM_CODE
u32 
RENDER_getFrame(void)
{
    return _RENDER_frame;
}

IWRAM_CODE
u16 *
RENDER_getDrawBuffer(void)
{
    return _RENDER_drawBuf;
}
