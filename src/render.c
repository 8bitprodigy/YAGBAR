#include "data.h"
#include "math.h"
#include "render.h"

#include "reciprocal_table.h"


#define _RENDER_UNUSED(what) (void)(what);


/*******************************************************************************
	Global helper variables, for precomputing stuff etc.
*******************************************************************************/
static u8                    _RENDER_visibleSprites[RENDER_NUM_VISIBLE_SPRITES];
static YAGBAR_Unit           _RENDER_spriteDist[RENDER_NUM_VISIBLE_SPRITES];
static u8                    _RENDER_spriteCount;
static YAGBAR_Unit           _RENDER_depthBuf[RENDER_W]      = {0};
static YAGBAR_Camera         _RENDER_camera;
static YAGBAR_Unit           _RENDER_horizontalDepthStep     = 0; 
static YAGBAR_Unit           _RENDER_startFloorHeight        = 0;
static YAGBAR_Unit           _RENDER_startCeil_Height        = 0;
static YAGBAR_Unit           _RENDER_camResYLimit            = 0;
static YAGBAR_Unit           _RENDER_middleRow               = 0;
static RENDER_ArrayFunction  _RENDER_floorFunction           = 0;
static RENDER_ArrayFunction  _RENDER_ceilFunction            = 0;
static YAGBAR_Unit           _RENDER_fHorizontalDepthStart   = 0;
static YAGBAR_Unit           _RENDER_cHorizontalDepthStart   = 0;
static s16                   _RENDER_cameraHeightScreen      = 0;
static RENDER_ArrayFunction  _RENDER_rollFunction            = 0; // says door rolling
static YAGBAR_Unit          *_RENDER_floorPixelDistances     = 0;
static YAGBAR_Unit           _RENDER_fovCorrectionFactors[2] = {0,0}; //correction for hor/vert fov
static YAGBAR_Unit           _RENDER_fovScale                = 0;
static u32                   _RENDER_frame                   = 0;
static u16                  *_RENDER_drawBuf;
static u8                    _RENDER_page                    = 0;


static inline 
YAGBAR_Unit 
_RENDER_fovCorrectionFactor(YAGBAR_Unit fov);


/*******************************************************************************
    PRIVATE FUNCTIONS
*******************************************************************************/
IWRAM_CODE
static inline
void
sortSprites(YAGBAR_Camera *cam)
{
    YAGBAR_Vec2 cam_dir = MATH_angleToDirection(cam->angle);
    _RENDER_spriteCount = 0;
    /* Cull and compute distances */
    for (u8 i = 0; i < YAGBAR_entityCount; i++) {
        YAGBAR_Entity *ent = &YAGBAR_entities[i];

        YAGBAR_Unit
            dx = ent->position.x - cam->position.x,
            dy = ent->position.y - cam->position.y;

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
        YAGBAR_Unit key_dist = _RENDER_spriteDist[i];
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
drawSprites(YAGBAR_Camera *cam)
{
    sortSprites(cam);

    YAGBAR_Vec2 cam_dir = MATH_angleToDirection(cam->angle);
    YAGBAR_Unit 
        cam_right_x =  cam_dir.y,
        cam_right_y = -cam_dir.x;

    for (u8 si = 0; si < _RENDER_spriteCount; si++) {
        YAGBAR_Entity *ent = &YAGBAR_entities[_RENDER_visibleSprites[si]];

        /* Transform into camera space */
        YAGBAR_Unit 
            rel_x = ent->position.x - cam->position.x,
            rel_y = ent->position.y - cam->position.y,

            depth  = (rel_x * cam_dir.x   + rel_y * cam_dir.y)   
                    / YAGBAR_UNITS_PER_SQUARE,
            strafe = (rel_x * cam_right_x + rel_y * cam_right_y) 
                    / YAGBAR_UNITS_PER_SQUARE;

        if (depth <= 0) continue;

        const YAGBAR_Unit RW = cam->resolution.x;
        const YAGBAR_Unit RH = cam->resolution.y;
        /* Screen X center */
        YAGBAR_Unit fov_depth = MATH_fast_div(depth * RENDER_HORIZONTAL_FOV_TAN, YAGBAR_UNITS_PER_SQUARE);
        int screen_x = RENDER_W / 2
            + (int)MATH_fast_div(strafe * (RENDER_W >> 1), fov_depth);

        /* Screen height */
        int sprite_h = (int)RENDER_perspectiveScaleVertical(RH, depth);
        if (sprite_h <= 0) continue;

        int
            sprite_w = MATH_fast_div((sprite_h * RW), RH),

            draw_x0 = screen_x          - (sprite_w >> 1),
            draw_x1 = screen_x          + (sprite_w >> 1);
        YAGBAR_Unit world_offset = YAGBAR_UNITS_PER_SQUARE - cam->height;
        int 
            screen_center_y = _RENDER_middleRow 
                    - (int)RENDER_perspectiveScaleVertical(world_offset, depth) 
                    * RH / YAGBAR_UNITS_PER_SQUARE,

            draw_y0 = screen_center_y - (sprite_h >> 1),
            draw_y1 = screen_center_y + (sprite_h >> 1);

        const u8 *tex = DATA_spriteTextures[ent->sprite_index];
        u8
            spr_w = tex[0],
            spr_h = tex[1];
        const u8 *texture = tex + 2;

        int sx0 = -1, sx1 = -1;
        int cur0 = -1, cur1 = -1;

        for (int sx = draw_x0; sx < draw_x1; sx++) {
            if (sx < 0 || sx >= RW) continue;
            if (depth < _RENDER_depthBuf[sx]) {
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
        u8 shade = (u8)(depth >> DEPTH_SHIFT_AMOUNT);

        for (int sy = draw_y0; sy < draw_y1; sy++) {
            if (sy < 0 || sy >= RH) continue;

            int ty = ((sy - draw_y0) * spr_h) / sprite_h;

            for (int sx = sx0; sx <= sx1; sx++) {
                int tx   = ((sx - draw_x0) * spr_w) / sprite_w;
                u8 color = texture[ty * spr_w + tx];
                if (color == RENDER_PAL_TRANSPARENT) continue;

                register u8  base       = color & 0xF0;
                register s16 shaded     = (s16)color - (s16)shade;
                shaded                 &= ~(shaded >> 15);
                register s16 below_base = shaded - (s16)base;
                shaded                 &= ~(below_base >> 15);
                color                   = (u8)shaded;

                u16 *addr = _RENDER_drawBuf + ((sy * (SCREEN_W >> 1)) + sx);
                *addr     = color | (color << 8);
            }
        }
    }
}


IWRAM_CODE
static inline u8
sampleTexture(
  const u8    *tex,
  YAGBAR_Unit  tx, 
  YAGBAR_Unit  ty
)
{
    const u8 w = 64;
    const u8 h = 64;
    tx = MATH_wrap(tx, YAGBAR_UNITS_PER_SQUARE);
    ty = MATH_wrap(ty, YAGBAR_UNITS_PER_SQUARE);
    register s16 px = (w * tx) >> 10;
    register s16 py = (h * ty) >> 10;
    return tex[2 + h * px + py];
}

// Called by raycastlib for every pixel on screen.
IWRAM_CODE 
static inline
void 
pixelFunc(RENDER_PixelInfo *p)
{
    register u8 color;
#if (DEPTH_SHADE_WALLS || DEPTH_SHADE_FLOOR || DEPTH_SHADE_CEILING || USE_SIDE_SHADING)
    register u8  base;
    register s8  shade;
    register s16 shaded;
    register s16 below_base;
#endif /* USE_DEPTH_SHADING || USE_SIDE_SHADING */
#ifndef RCL_COLUMN_FUNCTION
    if (p->is_wall) {
#endif /* RCL_COLUMN_FUNCTION */

#if !WALLS_TEXTURED    
        // Shade walls by depth and face direction.
        // depth is in RCL units; clamp to 3 shading levels.
        color = PAL_WALL;//3 - RCL_min(3, (int)(p->depth / YAGBAR_UNITS_PER_SQUARE));
#else
        color = sampleTexture(wall_tex, p->texCoords.x, p->texCoords.y);
#endif /* WALLS_UNTEXTURED */
        // Side-faces (N/S vs E/W) get a subtle brightness boost.
    #if (DEPTH_SHADE_WALLS || USE_SIDE_SHADING)
        shade  = 
        #if USE_SIDE_SHADING == 1
                1 - (p->hit.direction & 1)
        #else
                4 - (p->hit.direction)
        #endif /* USE_SIDE_SHADING */
        #if (USE_SIDE_SHADING && DEPTH_SHADE_WALLS)
                +
        #endif /* USE_SIDE_SHADING && DEPTH_SHADE_WALLS */
        #if DEPTH_SHADE_WALLS
                (p->depth >> DEPTH_SHIFT_AMOUNT)
        #endif /* DEPTH_SHADE_WALLS */
            ;
        /* Clamp color to within row */
        base        = color & 0xF0;
        shaded      = (s16)color - (s16)shade;
        shaded     &= ~(shaded >> 15);               // clamp to 0 if negative
        below_base  = shaded - (s16)base;
        shaded     &= ~(below_base >> 15);           // clamp to 0 if crossed row boundary
        color       = (u8)shaded;
    #endif /* DEPTH_SHADE_WALLS || USE_SIDE_SHADING */
#ifndef RCL_COLUMN_FUNCTION
    }
    else if (p->is_floor) {
    #if TEXTURED_FLOOR
        color = sampleTexture(ceil_tex, p->texCoords.x, p->texCoords.y);
    #elif COLORED_FLOOR
        register s16 tile_x = p->texCoords.x >> 10;
        register s16 tile_y = p->texCoords.y >> 10;
        color = floorColorMap[tile_y * LEVEL_W + tile_x];
    #else
        color = PAL_FLOOR;
    #endif /* TEXTURED_FLOORS */
    #if DEPTH_SHADE_FLOOR
        /* +((YAGBAR_UNITS_PER_SQUARE>>4) * 18) */
        shade = ((p->depth) >> (DEPTH_SHIFT_AMOUNT));
        /* Clamp color to within row */
        base        = color & 0xF0;
        shaded      = (s16)color - (s16)shade;
        shaded     &= ~(shaded >> 15);               // clamp to 0 if negative
        below_base  = shaded - (s16)base;
        shaded     &= ~(below_base >> 15);           // clamp to 0 if crossed row boundary
        color       = (u8)shaded;
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
        shade = ((p->depth) >> (DEPTH_SHIFT_AMOUNT));
        /* Clamp color to within row */
        base        = color & 0xF0;
        shaded      = (s16)color - (s16)shade;
        shaded     &= ~(shaded >> 15);               // clamp to 0 if negative
        below_base  = shaded - (s16)base;
        shaded     &= ~(below_base >> 15);           // clamp to 0 if crossed row boundary
        color       = (u8)shaded;
    #endif /* DEPTH_SHADE_CEILING */
    }
#endif /* RCL_COLUMN_FUNCTION */

    u16 *addr = _RENDER_drawBuf + ((p->position.y * SCREEN_W + p->position.x * 2) >> 1);
    *addr = (u16)color | ((u16)color << 8);
}



/*  Helper function that determines intersection with both ceiling and floor. 
*/
static inline
YAGBAR_Unit 
_RENDER_floorCeilFunction(s16 x, s16 y)
{
    YAGBAR_Unit f = _RENDER_floorFunction(x,y);

    if (_RENDER_ceilFunction == 0)
        return f;

    YAGBAR_Unit c = _RENDER_ceilFunction(x,y);

  return ((f & 0x00ff) << 8) | (c & 0x00ff);
}

static inline
YAGBAR_Unit 
_floorHeightNotZeroFunction(s16 x, s16 y)
{
    return (RENDER_RS_HEIGHT_FN(x,y) == 0) 
        ? 0 
        : MATH_nonZero((x & 0x00FF) | ((y & 0x00FF) << 8));
        // ^ this makes collisions between all squares - needed for rolling doors
}


/* Helper for drawing floor or ceiling. Returns the last drawn pixel position. 
*/
IWRAM_CODE
inline 
s16 
_RENDER_drawHorizontalColumn(
    YAGBAR_Unit    yCurrent,
    YAGBAR_Unit    yTo,
    YAGBAR_Unit    limit1, // TODO: s16?
    YAGBAR_Unit    limit2,
    YAGBAR_Unit    verticalOffset,
    s16            increment,
    s8             computeDepth,
    s8             computeCoords,
    s16            depthIncrementMultiplier,
    YAGBAR_Ray    *ray,
    RENDER_PixelInfo *pixelInfo
)
{
    _RENDER_UNUSED(ray);

    YAGBAR_Unit depthIncrement;
    YAGBAR_Unit dx;
    YAGBAR_Unit dy;

    pixelInfo->is_wall = 0;

    s16 limit = MATH_clamp(yTo,limit1,limit2);

    YAGBAR_Unit depth = 0; /* TODO: this is for clamping depth to 0 so that we don't
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
                dx = pixelInfo->hit.position.x - _RENDER_camera.position.x;\
                dy = pixelInfo->hit.position.y - _RENDER_camera.position.y;\
            }\
            for (s16 i = yCurrent + increment;\
                increment == -1 ? i >= limit : i <= limit; /* TODO: is efficient? */\
                i += increment)\
            {\
            pixelInfo->position.y = i;\
            if (doDepth) { /*constant condition - compiler should optimize it out*/\
                depth += depthIncrement;\
                pixelInfo->depth = MATH_zeroClamp(depth); \
                /* ^ int comparison is fast, it is not braching! (= test instr.) */\
            }\
            if (doCoords) { /*constant condition - compiler should optimize it out*/\
                YAGBAR_Unit d = _RENDER_floorPixelDistances[i];\
                YAGBAR_Unit d2 = MATH_nonZero(pixelInfo->hit.distance);\
                pixelInfo->texCoords.x =\
                _RENDER_camera.position.x +  MATH_fast_div((d * dx), d2);\
                pixelInfo->texCoords.y =\
                _RENDER_camera.position.y +  MATH_fast_div((d * dy), d2);\
            }\
            RENDER_PIXEL_FUNCTION(pixelInfo);\
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


/// Helper for drawing walls. Returns the last drawn pixel position.
IWRAM_CODE
static inline 
s16 
_RENDER_drawWall(
    YAGBAR_Unit yCurrent,
    YAGBAR_Unit yFrom,
    YAGBAR_Unit yTo,
    YAGBAR_Unit limit1, // TODO: s16?
    YAGBAR_Unit limit2,
    YAGBAR_Unit height,
    s16 increment,
    RENDER_PixelInfo *pixelInfo
)
{
    _RENDER_UNUSED(height)

    height = MATH_abs(height);

    pixelInfo->is_wall = 1;

    YAGBAR_Unit limit = MATH_clamp(yTo,limit1,limit2);

    YAGBAR_Unit wallLength = MATH_nonZero(MATH_abs(yTo - yFrom - 1));

    YAGBAR_Unit wallPosition = MATH_abs(yFrom - yCurrent) - increment;

    YAGBAR_Unit heightScaled = height * RENDER_TEXTURE_INTERPOLATION_SCALE;
    _RENDER_UNUSED(heightScaled);

    YAGBAR_Unit coordStepScaled = RENDER_COMPUTE_WALL_TEXCOORDS ?
#if RENDER_TEXTURE_VERTICAL_STRETCH == 1
    ((RENDER_UNITS_PER_SQUARE * RENDER_TEXTURE_INTERPOLATION_SCALE) / wallLength)
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
        (RENDER_UNITS_PER_SQUARE * RENDER_TEXTURE_INTERPOLATION_SCALE)
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

    YAGBAR_Unit textureCoordScaled = pixelInfo->texCoords.y;

    for (YAGBAR_Unit i = yCurrent + increment; 
        increment == -1 ? i >= limit : i <= limit; // TODO: is efficient?
        i += increment)
    {
        pixelInfo->position.y = i;

#if RENDER_COMPUTE_WALL_TEXCOORDS == 1
        pixelInfo->texCoords.y = MATH_fast_div(
                textureCoordScaled,
                RENDER_TEXTURE_INTERPOLATION_SCALE
            );

        textureCoordScaled += coordStepScaled;
#endif

        RENDER_PIXEL_FUNCTION(pixelInfo);
    }

    return limit;
}

/// Fills a RENDER_HitResult struct with info for a hit at infinity.
IWRAM_CODE
static inline 
void 
_RENDER_makeInfiniteHit(RENDER_HitResult *hit, YAGBAR_Ray *ray)
{
    hit->distance = RENDER_UNITS_PER_SQUARE * RENDER_UNITS_PER_SQUARE;
    /* ^ horizon is at infinity, but we can't use too big infinity
        (YAGBAR_INFINITY) because it would overflow in the following mult. */
    hit->position.x = MATH_fast_div(
            (ray->direction.x * hit->distance), 
            RENDER_UNITS_PER_SQUARE
        );
    hit->position.y = MATH_fast_div(
            (ray->direction.y * hit->distance), 
            RENDER_UNITS_PER_SQUARE
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
_RENDER_columnFunctionComplex(
    RENDER_HitResult *hits, 
    u16 hitCount, 
    u16 x,
    YAGBAR_Ray ray
)
{
    // last written Y position, can never go backwards
    YAGBAR_Unit fPosY = _RENDER_camera.resolution.y;
    YAGBAR_Unit cPosY = -1;

    // world coordinates (relative to camera height though)
    YAGBAR_Unit fZ1World = _RENDER_startFloorHeight;
    YAGBAR_Unit cZ1World = _RENDER_startCeil_Height;

    RENDER_PixelInfo p;
    p.position.x = x;
    p.height = 0;
    p.wall_height = 0;
    p.texCoords.x = 0;
    p.texCoords.y = 0;

    // we'll be simulatenously drawing the floor and the ceiling now  
    for (YAGBAR_Unit j = 0; j <= hitCount; ++j) {                    
        // ^ = add extra iteration for horizon plane
        s8 drawingHorizon = j == hitCount;

        RENDER_HitResult hit;
        YAGBAR_Unit distance = 1;

        YAGBAR_Unit fWallHeight = 0, cWallHeight = 0;
        YAGBAR_Unit fZ2World = 0,    cZ2World = 0;
        YAGBAR_Unit fZ1Screen = 0,   cZ1Screen = 0;
        YAGBAR_Unit fZ2Screen = 0,   cZ2Screen = 0;

        if (!drawingHorizon) {
            hit = hits[j];
            distance = MATH_nonZero(hit.distance); 
            p.hit = hit;

            fWallHeight = _RENDER_floorFunction(hit.square.x,hit.square.y);
            fZ2World = fWallHeight - _RENDER_camera.height;
            fZ1Screen = _RENDER_middleRow - RENDER_perspectiveScaleVertical(
                    MATH_fast_div(
                            (fZ1World * _RENDER_camera.resolution.y),
                            RENDER_UNITS_PER_SQUARE
                        ),
                    distance
                );
            fZ2Screen = _RENDER_middleRow - RENDER_perspectiveScaleVertical(
                    MATH_fast_div(
                            (fZ2World * _RENDER_camera.resolution.y),
                            RENDER_UNITS_PER_SQUARE
                        ),
                    distance
                );

            if (_RENDER_ceilFunction != 0) {
                cWallHeight = _RENDER_ceilFunction(hit.square.x,hit.square.y);
                cZ2World = cWallHeight - _RENDER_camera.height;
                cZ1Screen = _RENDER_middleRow - RENDER_perspectiveScaleVertical(
                        MATH_fast_div(
                                (cZ1World * _RENDER_camera.resolution.y),
                                RENDER_UNITS_PER_SQUARE
                            ),
                        distance
                    );
                cZ2Screen = _RENDER_middleRow - RENDER_perspectiveScaleVertical(
                        MATH_fast_div(
                                (cZ2World * _RENDER_camera.resolution.y),
                                RENDER_UNITS_PER_SQUARE
                            ),
                        distance
                    );
            }
        }
        else {
            fZ1Screen = _RENDER_middleRow;
            cZ1Screen = _RENDER_middleRow + 1;
            _RENDER_makeInfiniteHit(&p.hit,&ray);
        }

        YAGBAR_Unit limit;

        p.is_wall = 0;
        p.is_horizon = drawingHorizon;

        // draw floor until wall
        p.is_floor = 1;
        p.height = fZ1World + _RENDER_camera.height;
        p.wall_height = 0;

#if RENDER_COMPUTE_FLOOR_DEPTH == 1
        p.depth = (_RENDER_fHorizontalDepthStart - fPosY) * _RENDER_horizontalDepthStep;
#else
        p.depth = 0;
#endif /* RENDER_COMPUTE_FLOOR_DEPTH */

    limit = _RENDER_drawHorizontalColumn(
            fPosY,
            fZ1Screen,
            cPosY + 1,
            _RENDER_camera.resolution.y,
            fZ1World,
            -1,
            RENDER_COMPUTE_FLOOR_DEPTH,
            // ^ purposfully allow outside screen bounds
            RENDER_COMPUTE_FLOOR_TEXCOORDS && p.height == RENDER_FLOOR_TEXCOORDS_HEIGHT,
            1,
            &ray,
            &p
        );

    if (fPosY > limit)
        fPosY = limit;

    if (_RENDER_ceilFunction != 0 || drawingHorizon) {
        // draw ceiling until wall
        p.is_floor = 0;
        p.height = cZ1World + _RENDER_camera.height;

#if RENDER_COMPUTE_CEILING_DEPTH == 1
        p.depth = (cPosY - _RENDER_cHorizontalDepthStart)
            * _RENDER_horizontalDepthStep;
#endif /* RENDER_COMPUTE_CEILING_DEPTH */

        limit = _RENDER_drawHorizontalColumn(
                cPosY,
                cZ1Screen,
                -1,
                fPosY - 1,
                cZ1World,
                1,
                RENDER_COMPUTE_CEILING_DEPTH,
                0,
                1,
                &ray,
                &p
            );
        // ^ purposfully allow outside screen bounds here

        if (cPosY < limit)
            cPosY = limit;
    }

    if (!drawingHorizon) { // don't draw walls for horizon plane
        p.is_wall = 1;
        p.depth = distance;
        p.is_floor = 1;
        p.texCoords.x = hit.texture_coord;
        p.height = fZ1World + _RENDER_camera.height;
        p.wall_height = fWallHeight;

        // draw floor wall

        if (fPosY > 0) { // still pixels left?
        
            p.is_floor = 1;

            limit = _RENDER_drawWall(
                    fPosY,
                    fZ1Screen,
                    fZ2Screen,
                    cPosY + 1,
                    _RENDER_camera.resolution.y,
                    // ^ purposfully allow outside screen bounds here
#if RENDER_TEXTURE_VERTICAL_STRETCH == 1
                  RENDER_UNITS_PER_SQUARE
#else
                  fZ2World - fZ1World
#endif /* RENDER_TEXTURE_VERTICAL_STRETCH */
                  ,-1,
                  &p
                  );
                

        if (fPosY > limit)
            fPosY = limit;

            fZ1World = fZ2World; // for the next iteration
        }               // ^ purposfully allow outside screen bounds here

      // draw ceiling wall

        if (_RENDER_ceilFunction != 0 && cPosY < _RENDER_camResYLimit) { // pixels left?

            p.is_floor = 0;
            p.height = cZ1World + _RENDER_camera.height;
            p.wall_height = cWallHeight;

            limit = _RENDER_drawWall(
                    cPosY,
                    cZ1Screen,
                    cZ2Screen,
                    -1,
                    fPosY - 1,
                    // ^ puposfully allow outside screen bounds here
#if RENDER_TEXTURE_VERTICAL_STRETCH == 1
                  RENDER_UNITS_PER_SQUARE
#else
                    cZ1World - cZ2World 
#endif /* RENDER_TEXTURE_VERTICAL_STRETCH */
                    ,1,
                    &p
                );
                        
                if (cPosY < limit)
                    cPosY = limit;

                cZ1World = cZ2World; // for the next iteration
            }              // ^ puposfully allow outside screen bounds here 
        }
    }
}


IWRAM_CODE
static inline
void 
_RENDER_columnFunctionSimple(
    RENDER_HitResult *hits, 
    u16 hitCount,
    u16 x, 
    YAGBAR_Ray ray
)
{
    YAGBAR_Unit y = 0;
    YAGBAR_Unit wallHeightScreen = 0;
    YAGBAR_Unit wallStart = _RENDER_middleRow;

    YAGBAR_Unit dist = 1;

    RENDER_PixelInfo p;
    p.position.x = x;
    p.wall_height = RENDER_UNITS_PER_SQUARE;

    if (hitCount > 0) {
        RENDER_HitResult hit = hits[0];
        _RENDER_depthBuf[x] = hit.distance;

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

                YAGBAR_Unit texCoordMod = MATH_fast_mod(hit.texture_coord, RENDER_UNITS_PER_SQUARE);

                s8 unrolled = hit.doorRoll >= 0 ?
                (hit.doorRoll > texCoordMod) :
                (texCoordMod > RENDER_UNITS_PER_SQUARE + hit.doorRoll);

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

            YAGBAR_Unit wallHeightWorld = _RENDER_floorFunction(hit.square.x,hit.square.y);

            if (wallHeightWorld < 0) {
                /* We can't just do wallHeightWorld = max(0,wallHeightWorld) because
                we would be processing an actual hit with height 0, which shouldn't
                ever happen, so we assign some arbitrary height. */
                wallHeightWorld = RENDER_UNITS_PER_SQUARE;
            }

            YAGBAR_Unit worldPointTop = wallHeightWorld - _RENDER_camera.height;
            YAGBAR_Unit worldPointBottom = -1 * _RENDER_camera.height;

            wallStart = _RENDER_middleRow - MATH_fast_div(
                    (
                            RENDER_perspectiveScaleVertical(worldPointTop,dist)
                            * _RENDER_camera.resolution.y
                        ),
                    RENDER_UNITS_PER_SQUARE
                );

            s16 wallEnd =  _RENDER_middleRow - MATH_fast_div(
                    (
                            RENDER_perspectiveScaleVertical(worldPointBottom,dist)
                            * _RENDER_camera.resolution.y
                        ), 
                    RENDER_UNITS_PER_SQUARE
                );

            wallHeightScreen = wallEnd - wallStart;

            if (wallHeightScreen <= 0) // can happen because of rounding errors
                wallHeightScreen = 1; 
        }
    }
    else {
        _RENDER_makeInfiniteHit(&p.hit,&ray);
        _RENDER_depthBuf[x] = YAGBAR_INFINITY;
    }

    // draw ceiling
#ifdef RENDER_COLUMN_FUNCTION
    RENDER_COLUMN_FUNCTION(x, 0, wallStart - 1, 0);
    y = wallStart;
#else

    p.is_wall = 0;
    p.is_floor = 0;
    p.is_horizon = 1;
    p.depth = 1;
    p.height = RENDER_UNITS_PER_SQUARE;
    y = _RENDER_drawHorizontalColumn(
            -1,
            wallStart,
            -1,
            _RENDER_middleRow,
            _RENDER_camera.height,
            1,
            RENDER_COMPUTE_CEILING_DEPTH,
            0,
            1,
            &ray,
            &p
        );
#endif /* RENDER_COLUMN_FUNCTION */

    // draw wall

    p.is_wall = 1;
    p.is_floor = 1;
    p.depth = dist;
    p.height = 0;

#if RENDER_ROLL_TEXTURE_COORDS == 1 && RENDER_COMPUTE_WALL_TEXCOORDS == 1 
    p.hit.texture_coord -= p.hit.doorRoll;
#endif /* RENDER_ROLL_TEXTURE_COORDS == 1 && RENDER_COMPUTE_WALL_TEXCOORDS == 1  */

    p.texCoords.x = p.hit.texture_coord;
    p.texCoords.y = 0;

    YAGBAR_Unit limit = _RENDER_drawWall(
            y,wallStart,
            wallStart + wallHeightScreen - 1,
            -1,
            _RENDER_camResYLimit,
            p.hit.array_value,
            1,
            &p
        );

    y = MATH_max(y,limit); // take max, in case no wall was drawn
    y = MATH_max(y,wallStart);

    // draw floor
#ifdef RENDER_COLUMN_FUNCTION
    RENDER_COLUMN_FUNCTION(x, y, _RENDER_camResYLimit, 1);
#else
    p.is_wall = 0;

    #if RENDER_COMPUTE_FLOOR_DEPTH == 1
    p.depth = (_RENDER_camera.resolution.y - y) * _RENDER_horizontalDepthStep + 1;
    #endif

    _RENDER_drawHorizontalColumn(
            y,_RENDER_camResYLimit,
            -1,
            _RENDER_camResYLimit,
            _RENDER_camera.height,
            1,
            RENDER_COMPUTE_FLOOR_DEPTH,
            RENDER_COMPUTE_FLOOR_TEXCOORDS,
            -1,
            &ray,
            &p
        );
#endif /* RENDER_COLUMN_FUNCTION */
}

/*
  Precomputes a distance from camera to the floor at each screen row into an
  array (must be preallocated with sufficient (camera.resolution.y) length).
*/
IWRAM_CODE 
static inline 
void _RENDER_precomputeFloorDistances(
    YAGBAR_Camera camera,
    YAGBAR_Unit *dest, 
    u16 startIndex
)
{
    YAGBAR_Unit camHeightScreenSize =  MATH_fast_div(
            (camera.height * camera.resolution.y), 
            RENDER_UNITS_PER_SQUARE
        );

    for (u16 i = startIndex; i < camera.resolution.y; ++i)
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
YAGBAR_Unit 
_RENDER_fovCorrectionFactor(YAGBAR_Unit fov)
{
  u16 table[9] = 
    {1,208,408,692,1024,1540,2304,5376,30000};

  fov = MATH_min((RENDER_UNITS_PER_SQUARE >> 1) - 1,fov);

  u8  index = fov >> 6;
  u32 t     = ((fov - index * 64) * RENDER_UNITS_PER_SQUARE) >> 6; 
  u32 v1    = table[index];
  u32 v2    = table[index + 1];
 
  return v1 + MATH_fast_div(((v2 - v1) * t), RENDER_UNITS_PER_SQUARE);
}


/*******************************************************************************
    PUBLIC METHODS
*******************************************************************************/

IWRAM_CODE
static inline
void 
RENDER_castRayMultiHit(
    YAGBAR_Ray ray, 
    RENDER_ArrayFunction arrayFunc,
    RENDER_ArrayFunction typeFunc, 
    RENDER_HitResult *hitResults,
    u16 *hitResultsLen, 
    YAGBAR_RayConstraints constraints
)
{
    YAGBAR_Vec2 currentPos = ray.start;
    YAGBAR_Vec2 currentSquare;

    currentSquare.x = MATH_divRoundDown(ray.start.x,RENDER_UNITS_PER_SQUARE);
    currentSquare.y = MATH_divRoundDown(ray.start.y,RENDER_UNITS_PER_SQUARE);

    *hitResultsLen = 0;

    YAGBAR_Unit squareType = arrayFunc(currentSquare.x,currentSquare.y);

    // DDA variables
    YAGBAR_Vec2 nextSideDist; // dist. from start to the next side in given axis
    YAGBAR_Vec2 delta;
    YAGBAR_Vec2 step;         // -1 or 1 for each axis
    s8 stepHorizontal = 0; // whether the last step was hor. or vert.

    nextSideDist.x = 0;
    nextSideDist.y = 0;

    YAGBAR_Unit dirVecLengthNorm = MATH_len(ray.direction) * RENDER_UNITS_PER_SQUARE;

    delta.x = MATH_abs(dirVecLengthNorm / MATH_nonZero(ray.direction.x));
    delta.y = MATH_abs(dirVecLengthNorm / MATH_nonZero(ray.direction.y));

    // init DDA

    if (ray.direction.x < 0) {
        step.x = -1;
        nextSideDist.x = MATH_fast_div(
                (MATH_wrap(ray.start.x,RENDER_UNITS_PER_SQUARE) * delta.x),
                RENDER_UNITS_PER_SQUARE
            );
    }
    else {
        step.x = 1;
        nextSideDist.x = MATH_fast_div(
                ( (
                    MATH_wrap(
                            RENDER_UNITS_PER_SQUARE 
                            - ray.start.x,RENDER_UNITS_PER_SQUARE
                        ) ) * delta.x
                    ),
                RENDER_UNITS_PER_SQUARE
            );
    }

    if (ray.direction.y < 0)
    {
        step.y = -1;
        nextSideDist.y = MATH_fast_div(
                (MATH_wrap(ray.start.y,RENDER_UNITS_PER_SQUARE) * delta.y),
                RENDER_UNITS_PER_SQUARE
            );
    }
    else
    {
        step.y = 1;
        nextSideDist.y = MATH_fast_div(
                ( (
                    MATH_wrap(
                            RENDER_UNITS_PER_SQUARE - ray.start.y,
                            RENDER_UNITS_PER_SQUARE
                        )) * delta.y
                    ),
                RENDER_UNITS_PER_SQUARE
            );
    }

    // DDA loop

    #define RECIP_SCALE 65536

    YAGBAR_Unit dx = MATH_nonZero(ray.direction.x);
    YAGBAR_Unit dy = MATH_nonZero(ray.direction.y);
    YAGBAR_Unit rayDirXRecip = depth_reciprocal_65536[MATH_abs(dx)]; if (dx < 0) rayDirXRecip = -rayDirXRecip;
    YAGBAR_Unit rayDirYRecip = depth_reciprocal_65536[MATH_abs(dy)]; if (dy < 0) rayDirYRecip = -rayDirYRecip;
    // ^ we precompute reciprocals to avoid divisions in the loop

    for (u16 i = 0; i < constraints.max_steps; ++i) {
        YAGBAR_Unit currentType = RENDER_RS_HEIGHT_FN(currentSquare.x,currentSquare.y);

        if (MATH_unlikely(currentType != squareType)) {
            // collision
            RENDER_HitResult h;

            h.array_value = currentType;
            h.doorRoll = 0;
            h.position = currentPos;
            h.square   = currentSquare;

            if (stepHorizontal) {
                h.position.x = currentSquare.x * RENDER_UNITS_PER_SQUARE;
                h.direction = 3;

                if (step.x == -1)
                {
                h.direction = 1;
                h.position.x += RENDER_UNITS_PER_SQUARE;
                }

                YAGBAR_Unit diff = h.position.x - ray.start.x;

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
    YAGBAR_Unit tmp = diff >> 2;        /* 4 to prevent overflow */ \
    h.distance = ((tmp >> 3) != 0) /* prevent a bug with small dists */ \
        ? MATH_fast_div((tmp * RENDER_UNITS_PER_SQUARE * rayDir ## dir1 ## Recip), (RECIP_SCALE >> 2))\
        : MATH_abs(h.position.dir2 - ray.start.dir2)

                CORRECT(X,y);

#endif // RENDER_RECTILINEAR
            }
            else {
                h.position.y = currentSquare.y * RENDER_UNITS_PER_SQUARE;
                h.direction = 2;

                if (step.y == -1) {
                h.direction = 0;
                h.position.y += RENDER_UNITS_PER_SQUARE;
                }

                YAGBAR_Unit diff = h.position.y - ray.start.y;

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
                MATH_wrap(-1 * h.position.x,RENDER_UNITS_PER_SQUARE); break;

            case 1: h.texture_coord =
                MATH_wrap(h.position.y,RENDER_UNITS_PER_SQUARE); break;

            case 2: h.texture_coord =
                MATH_wrap(h.position.x,RENDER_UNITS_PER_SQUARE); break;

            case 3: h.texture_coord =
                MATH_wrap(-1 * h.position.y,RENDER_UNITS_PER_SQUARE); break;

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
static inline
RENDER_HitResult 
RENDER_castRay(YAGBAR_Ray ray, RENDER_ArrayFunction arrayFunc)
{
    RENDER_HitResult result;
    u16 len;
    YAGBAR_RayConstraints c;

    c.max_steps = 1000;
    c.max_hits = 1;

    RENDER_castRayMultiHit(ray,arrayFunc,0,&result,&len,c);

    if (len == 0)
        result.distance = -1;

    return result;
}

IWRAM_CODE 
void 
RENDER_castRaysMultiHit(
    YAGBAR_Camera         cam, 
    RENDER_ArrayFunction  arrayFunc,
    RENDER_ArrayFunction  typeFunction, 
    RENDER_ColumnFunction columnFunc,
    YAGBAR_RayConstraints constraints
)
{
    YAGBAR_Vec2 dir1 =
        MATH_angleToDirection(cam.angle - RENDER_HORIZONTAL_FOV_HALF);

    YAGBAR_Vec2 dir2 =
        MATH_angleToDirection(cam.angle + RENDER_HORIZONTAL_FOV_HALF);

    /* We scale the side distances so that the middle one is
        RENDER_UNITS_PER_SQUARE, which has to be this way. */
    YAGBAR_Unit cos = MATH_nonZero(MATH_cos(RENDER_HORIZONTAL_FOV_HALF));

    dir1.x = MATH_fast_div((dir1.x * RENDER_UNITS_PER_SQUARE), cos);
    dir1.y = MATH_fast_div((dir1.y * RENDER_UNITS_PER_SQUARE), cos);

    dir2.x = MATH_fast_div((dir2.x * RENDER_UNITS_PER_SQUARE), cos);
    dir2.y = MATH_fast_div((dir2.y * RENDER_UNITS_PER_SQUARE), cos);

    YAGBAR_Unit dX = dir2.x - dir1.x;
    YAGBAR_Unit dY = dir2.y - dir1.y;

    RENDER_HitResult hits[constraints.max_hits];
    u16 hitCount;

    YAGBAR_Ray r;
    r.start = cam.position;

    YAGBAR_Unit currentDX = 0;
    YAGBAR_Unit currentDY = 0;

    for (s16 i = 0; i < cam.resolution.x; ++i) {
        /* Here by linearly interpolating the direction vector its length changes,
        which in result achieves correcting the fish eye effect (computing
        perpendicular distance). */

        r.direction.x = dir1.x + MATH_fast_div(currentDX, cam.resolution.x);
        r.direction.y = dir1.y + MATH_fast_div(currentDY, cam.resolution.x);

        RENDER_castRayMultiHit(r,arrayFunc,typeFunction,hits,&hitCount,constraints);

        columnFunc(hits,hitCount,i,r);

        currentDX += dX;
        currentDY += dY;
    }
}


void 
RENDER_renderComplex(
    YAGBAR_Camera         cam, 
    RENDER_ArrayFunction  floorHeightFunc,
    RENDER_ArrayFunction  ceilingHeightFunc, 
    RENDER_ArrayFunction  typeFunction,
    YAGBAR_RayConstraints constraints
)
{
    _RENDER_floorFunction = floorHeightFunc;
    _RENDER_ceilFunction = ceilingHeightFunc;
    _RENDER_camera = cam;
    _RENDER_camResYLimit = cam.resolution.y - 1;

    u16 halfResY = cam.resolution.y >> 1;

    _RENDER_middleRow = halfResY + cam.shear;

    _RENDER_fHorizontalDepthStart = _RENDER_middleRow + halfResY;
    _RENDER_cHorizontalDepthStart = _RENDER_middleRow - halfResY;

    _RENDER_startFloorHeight = floorHeightFunc(
        MATH_divRoundDown(cam.position.x,RENDER_UNITS_PER_SQUARE),
        MATH_divRoundDown(cam.position.y,RENDER_UNITS_PER_SQUARE)) -1 * cam.height;

    _RENDER_startCeil_Height = 
        ceilingHeightFunc != (0) 
            ? ceilingHeightFunc(
                    MATH_divRoundDown(cam.position.x,RENDER_UNITS_PER_SQUARE),
                    MATH_divRoundDown(cam.position.y,RENDER_UNITS_PER_SQUARE)
                ) -1 * cam.height
            : YAGBAR_INFINITY;

    _RENDER_horizontalDepthStep = MATH_fast_div(RENDER_HORIZON_DEPTH, cam.resolution.y); 

#if RENDER_COMPUTE_FLOOR_TEXCOORDS == 1
    YAGBAR_Unit floorPixelDistances[cam.resolution.y];
    _RENDER_precomputeFloorDistances(cam,floorPixelDistances,0);
    _RENDER_floorPixelDistances = floorPixelDistances; // pass to column function
#endif

    RENDER_castRaysMultiHit(cam,_RENDER_floorCeilFunction,typeFunction,
        _RENDER_columnFunctionComplex,constraints);
}

IWRAM_CODE 
void 
RENDER_renderSimple(
    YAGBAR_Camera        cam, 
    RENDER_ArrayFunction floorHeightFunc,
    RENDER_ArrayFunction typeFunc, 
    RENDER_ArrayFunction rollFunc,
    YAGBAR_RayConstraints constraints
)
{
    _RENDER_floorFunction = floorHeightFunc;
    _RENDER_camera        = cam;
    _RENDER_camResYLimit  = cam.resolution.y - 1;
    _RENDER_middleRow     = cam.resolution.y >> 1;
    _RENDER_rollFunction  = rollFunc;

    _RENDER_cameraHeightScreen = MATH_fast_div(
            (
                    _RENDER_camera.resolution.y 
                    * (_RENDER_camera.height - RENDER_UNITS_PER_SQUARE)
                ),
            RENDER_UNITS_PER_SQUARE
        );

    _RENDER_horizontalDepthStep = MATH_fast_div(RENDER_HORIZON_DEPTH, cam.resolution.y); 

    constraints.max_hits = (_RENDER_rollFunction == 0)
        ? 1  // no door => 1 hit is enough 
        : 3; // for correctly rendering rolling doors we'll need 3 hits (NOT 2)

    #if RENDER_COMPUTE_FLOOR_TEXCOORDS == 1
    YAGBAR_Unit floorPixelDistances[cam.resolution.y];
    _RENDER_precomputeFloorDistances(cam,floorPixelDistances,_RENDER_middleRow);
    _RENDER_floorPixelDistances = floorPixelDistances; // pass to column function
    #endif

    if (_RENDER_fovCorrectionFactors[1] == 0)
        _RENDER_fovCorrectionFactors[1] = _RENDER_fovCorrectionFactor(RENDER_VERTICAL_FOV);

    _RENDER_fovScale = MATH_fast_div(
        _RENDER_fovCorrectionFactors[1],
        RENDER_UNITS_PER_SQUARE
    );

    RENDER_castRaysMultiHit(
            cam,
            _floorHeightNotZeroFunction,
            typeFunc,
            _RENDER_columnFunctionSimple, 
            constraints
        );

#if RENDER_COMPUTE_FLOOR_TEXCOORDS == 1
    _RENDER_floorPixelDistances = 0;
#endif

    drawSprites(&cam);
}

IWRAM_CODE 
YAGBAR_Unit 
RENDER_perspectiveScaleVertical(YAGBAR_Unit originalSize, YAGBAR_Unit distance)
{
  if (_RENDER_fovCorrectionFactors[1] == 0)
    _RENDER_fovCorrectionFactors[1] = _RENDER_fovCorrectionFactor(RENDER_VERTICAL_FOV);

  return distance != 0 
        ? MATH_fast_div(
                (originalSize * YAGBAR_UNITS_PER_SQUARE),
                MATH_nonZero(
                        MATH_fast_div(
                                (_RENDER_fovCorrectionFactors[1] * distance), 
                                YAGBAR_UNITS_PER_SQUARE
                            )
                    )
            ) 
        : 0;
}

IWRAM_CODE 
YAGBAR_Unit
RENDER_perspectiveScaleHorizontal(YAGBAR_Unit originalSize, YAGBAR_Unit distance)
{
    if (_RENDER_fovCorrectionFactors[0] == 0)
        _RENDER_fovCorrectionFactors[0] = _RENDER_fovCorrectionFactor(RENDER_HORIZONTAL_FOV);

    return (distance != 0) 
        ? MATH_fast_div(
                (originalSize * RENDER_UNITS_PER_SQUARE),
                MATH_nonZero(
                        MATH_fast_div(
                                (_RENDER_fovCorrectionFactors[0] * distance), 
                                RENDER_UNITS_PER_SQUARE
                            )
                    )
            )
        : 0;
}

IWRAM_CODE 
YAGBAR_Unit 
RENDER_perspectiveScaleHorizontalInverse(
    YAGBAR_Unit originalSize,
    YAGBAR_Unit scaledSize
)
{
  // TODO: probably doesn't work

  return scaledSize != 0 
    ? MATH_fast_div(
            (originalSize * RENDER_UNITS_PER_SQUARE + (RENDER_UNITS_PER_SQUARE >> 1)),
            MATH_fast_div(
                    (RENDER_HORIZONTAL_FOV_TAN * 2 * scaledSize), 
                    RENDER_UNITS_PER_SQUARE
                )
        )       
    : YAGBAR_INFINITY;
}

IWRAM_CODE 
YAGBAR_Unit 
RENDER_castRay3D(
    YAGBAR_Vec2       pos1, 
    YAGBAR_Unit           height1, 
    YAGBAR_Vec2       pos2, 
    YAGBAR_Unit           height2,
    RENDER_ArrayFunction  floorHeightFunc, 
    RENDER_ArrayFunction  ceilingHeightFunc,
    YAGBAR_RayConstraints constraints
)
{
    RENDER_HitResult hits[constraints.max_hits];
    u16 numHits;

    YAGBAR_Ray ray;

    ray.start = pos1;

    YAGBAR_Unit distance;

    ray.direction.x = pos2.x - pos1.x;
    ray.direction.y = pos2.y - pos1.y;

    distance = MATH_len(ray.direction);

    ray.direction = MATH_normalize(ray.direction); 

    YAGBAR_Unit heightDiff = height2 - height1;

    RENDER_castRayMultiHit(ray,floorHeightFunc,0,hits,&numHits,constraints);

    YAGBAR_Unit result = RENDER_UNITS_PER_SQUARE;

    s16 squareX = MATH_divRoundDown(pos1.x,RENDER_UNITS_PER_SQUARE);
    s16 squareY = MATH_divRoundDown(pos1.y,RENDER_UNITS_PER_SQUARE);

    YAGBAR_Unit startHeight = floorHeightFunc(squareX,squareY);

    #define checkHits(comp,res) \
    { \
        YAGBAR_Unit currentHeight = startHeight; \
        for (u16 i = 0; i < numHits; ++i) \
        { \
        if (hits[i].distance > distance) \
            break;\
        YAGBAR_Unit h = hits[i].array_value; \
        if ((currentHeight comp h ? currentHeight : h) \
            comp (height1 +  MATH_fast_div((hits[i].distance * heightDiff), distance))) \
        { \
            res =  MATH_fast_div((hits[i].distance * RENDER_UNITS_PER_SQUARE), distance); \
            break; \
        } \
        currentHeight = h; \
        } \
    }

    checkHits(>,result)

    if (ceilingHeightFunc != 0)
    {
        YAGBAR_Unit result2 = RENDER_UNITS_PER_SQUARE;
    
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
    YAGBAR_Camera *camera, 
    YAGBAR_Vec2 planeOffset,
    YAGBAR_Unit heightOffset, 
    RENDER_ArrayFunction floorHeightFunc,
    RENDER_ArrayFunction ceilingHeightFunc, 
    s8 computeHeight, 
    s8 force
)
{
    s8 movesInPlane = planeOffset.x != 0 || planeOffset.y != 0;

    if (movesInPlane || force) {
        s16 xSquareNew, ySquareNew;

        YAGBAR_Vec2 corner; // BBox corner in the movement direction
        YAGBAR_Vec2 cornerNew;

        s16 xDir = planeOffset.x > 0 ? 1 : -1;
        s16 yDir = planeOffset.y > 0 ? 1 : -1;

        corner.x = camera->position.x + xDir * RENDER_CAMERA_COLL_RADIUS;
        corner.y = camera->position.y + yDir * RENDER_CAMERA_COLL_RADIUS;

        s16 xSquare = MATH_divRoundDown(corner.x,RENDER_UNITS_PER_SQUARE);
        s16 ySquare = MATH_divRoundDown(corner.y,RENDER_UNITS_PER_SQUARE);

        cornerNew.x = corner.x + planeOffset.x;
        cornerNew.y = corner.y + planeOffset.y;

        xSquareNew = MATH_divRoundDown(cornerNew.x,RENDER_UNITS_PER_SQUARE);
        ySquareNew = MATH_divRoundDown(cornerNew.y,RENDER_UNITS_PER_SQUARE);

        YAGBAR_Unit bottomLimit = -1 * YAGBAR_INFINITY;
        YAGBAR_Unit topLimit = YAGBAR_INFINITY;

        YAGBAR_Unit currCeilHeight = YAGBAR_INFINITY;

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
        YAGBAR_Unit height = floorHeightFunc(s1,s2);\
        if ( \
            height > bottomLimit \
            || currCeilHeight - height \
            <  RENDER_CAMERA_COLL_HEIGHT_BELOW + RENDER_CAMERA_COLL_HEIGHT_ABOVE \
        )\
            dir##Collides = 1;\
        else if (ceilingHeightFunc != 0) { \
            YAGBAR_Unit height2 = ceilingHeightFunc(s1,s2); \
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
                * RENDER_CAMERA_COLL_RADIUS * 2,RENDER_UNITS_PER_SQUARE \
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
                cornerNew.dir = (dir##Square) * RENDER_UNITS_PER_SQUARE \
                        + (RENDER_UNITS_PER_SQUARE >> 1) + dir##Dir \
                        * (RENDER_UNITS_PER_SQUARE >> 1) - dir##Dir \

                collHandle(x);
                collHandle(y);
            
                #undef collHandle
            }
            else {
                /* Player collides without moving in the plane; this can happen e.g. on
                elevators due to vertical only movement. This code can get executed
                when force == 1. */

                YAGBAR_Vec2 squarePos;
                YAGBAR_Vec2 newPos;

                squarePos.x = xSquare * RENDER_UNITS_PER_SQUARE;
                squarePos.y = ySquare * RENDER_UNITS_PER_SQUARE;

                newPos.x = MATH_max(
                        squarePos.x + RENDER_CAMERA_COLL_RADIUS + 1,
                        MATH_min(
                                squarePos.x + RENDER_UNITS_PER_SQUARE 
                                        - RENDER_CAMERA_COLL_RADIUS - 1,
                                camera->position.x
                            )
                    );

                newPos.y = MATH_max(
                        squarePos.y + RENDER_CAMERA_COLL_RADIUS + 1,
                        MATH_min(
                                squarePos.y + RENDER_UNITS_PER_SQUARE 
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
                RENDER_UNITS_PER_SQUARE
            );

        s16 xSquare2 = MATH_divRoundDown(
                camera->position.x + RENDER_CAMERA_COLL_RADIUS,
                RENDER_UNITS_PER_SQUARE
            );

        s16 ySquare1 = MATH_divRoundDown(
                camera->position.y - RENDER_CAMERA_COLL_RADIUS,
                RENDER_UNITS_PER_SQUARE
            );

        s16 ySquare2 = MATH_divRoundDown(
                camera->position.y + RENDER_CAMERA_COLL_RADIUS,
                RENDER_UNITS_PER_SQUARE
            );

        YAGBAR_Unit bottomLimit = floorHeightFunc(xSquare1,ySquare1);
        YAGBAR_Unit topLimit    = (ceilingHeightFunc != 0) 
                ? ceilingHeightFunc(xSquare1,ySquare1) 
                : YAGBAR_INFINITY;

        YAGBAR_Unit height;

#define checkSquares(s1,s2)\
    { \
        height      = floorHeightFunc(xSquare##s1,ySquare##s2); \
        bottomLimit = MATH_max(bottomLimit,height); \
        height      = (ceilingHeightFunc != 0)  \
                ? ceilingHeightFunc(xSquare##s1,ySquare##s2)  \
                : YAGBAR_INFINITY;\
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
