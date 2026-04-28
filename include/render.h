#ifndef YGR_RENDER_H
#define YGR_RENDER_H


#include <tonc.h>

#include "core.h"


/*
    CONSTANTS / FLAGS
*/
#ifndef RENDER_COMPUTE_WALL_TEXCOORDS
    #define RENDER_COMPUTE_WALL_TEXCOORDS 1
#endif

#ifndef RENDER_COMPUTE_FLOOR_TEXCOORDS
    #define RENDER_COMPUTE_FLOOR_TEXCOORDS 0
#endif

#ifndef RENDER_FLOOR_TEXCOORDS_HEIGHT
    /*  If RENDER_COMPUTE_FLOOR_TEXCOORDS == 1,
        this says for what height level the
        texture coords will be computed for
        (for simplicity/performance only one
        level is allowed). */
    #define RENDER_FLOOR_TEXCOORDS_HEIGHT 0 
#endif


#ifndef RENDER_RECTILINEAR
    /*  Whether to use rectilinear perspective (normally
        used), or curvilinear perspective (fish eye). */
    #define RENDER_RECTILINEAR 1 
#endif

#ifndef RENDER_TEXTURE_VERTICAL_STRETCH
    /*  Whether textures should be
        stretched to wall height (possibly
        slightly slower if on). */
    #define RENDER_TEXTURE_VERTICAL_STRETCH 1 
#endif

#ifndef RENDER_COMPUTE_FLOOR_DEPTH
    /*  Whether depth should be computed for
        floor pixels - turns this off if not
        needed. */
    #define RENDER_COMPUTE_FLOOR_DEPTH 1 
#endif

#ifndef RENDER_COMPUTE_CEILING_DEPTH
    /*  As RENDER_COMPUTE_FLOOR_DEPTH but for
        ceiling. */
    #define RENDER_COMPUTE_CEILING_DEPTH 1 
#endif

#ifndef RENDER_ROLL_TEXTURE_COORDS
    /*  Says whether rolling doors should also
        roll the texture coordinates along (mostly
        desired for doors). */
    #define RENDER_ROLL_TEXTURE_COORDS 1 
#endif

#ifndef RENDER_VERTICAL_FOV
    #define RENDER_VERTICAL_FOV (RENDER_UNITS_PER_SQUARE / 3)
#endif

/* TAN approximation */
#define RENDER_VERTICAL_FOV_TAN (RENDER_VERTICAL_FOV * 4) 

#ifndef RENDER_HORIZONTAL_FOV
    #define RENDER_HORIZONTAL_FOV (RENDER_UNITS_PER_SQUARE >> 2)
#endif

#define RENDER_HORIZONTAL_FOV_TAN (RENDER_HORIZONTAL_FOV * 4)

#define RENDER_HORIZONTAL_FOV_HALF (RENDER_HORIZONTAL_FOV >> 1)

#ifndef RENDER_CAMERA_COLL_RADIUS
    #define RENDER_CAMERA_COLL_RADIUS (RENDER_UNITS_PER_SQUARE >> 2)
#endif

#ifndef RENDER_CAMERA_COLL_HEIGHT_BELOW
    #define RENDER_CAMERA_COLL_HEIGHT_BELOW RENDER_UNITS_PER_SQUARE
#endif 

#ifndef RENDER_CAMERA_COLL_HEIGHT_ABOVE
    #define RENDER_CAMERA_COLL_HEIGHT_ABOVE (RENDER_UNITS_PER_SQUARE / 3)
#endif

#ifndef RENDER_CAMERA_COLL_STEP_HEIGHT
    #define RENDER_CAMERA_COLL_STEP_HEIGHT (RENDER_UNITS_PER_SQUARE  >> 1)
#endif

#ifndef RENDER_TEXTURE_INTERPOLATION_SHIFT
    /*  This says scaling of fixed
        point vertical texture coord
        computation. This should be power
        of two! Higher number can look more 
        accurate but may cause overflow. */
    #define RENDER_TEXTURE_INTERPOLATION_SHIFT 10 
#endif

/*  What depth the
    horizon has (the floor
    depth is only
    approximated with the
    help of this
    constant). */
#define RENDER_HORIZON_DEPTH (11 * RENDER_UNITS_PER_SQUARE) 

#ifndef RENDER_VERTICAL_DEPTH_MULTIPLY
    /*  Defines a multiplier of height
        difference when approximating floor/ceil
        depth. */
    #define RENDER_VERTICAL_DEPTH_MULTIPLY 2 
#endif


#ifndef RENDER_FLOOR_HEIGHT_FN
    #define RENDER_FLOOR_HEIGHT_FN _floorHeightNotZeroFunction
#endif


/*******************************************************************************
    TYPEDEFS
*******************************************************************************/
typedef struct
{
    /*  Collided square coordinates. */
    YGR_Vec2 square;   
    /*  Exact collision position in YGR_Units. */
    YGR_Vec2 position;  
    /*  Distance to the hit position, or -1 if no collision happened. If 
        RENDER_RECTILINEAR != 0, then the distance is perpendicular to the 
        projection plane (fish eye correction), otherwise it is the straight 
        distance to the ray start position. */
    YGR_Unit distance; 
    /*  Normalized (0 to YGR_UNITS_PER_SQUARE - 1) texture coordinate 
        (horizontal). */
    YGR_Unit texture_coord; 
    /*  Value returned by array function (most often this will be the floor 
        height). */   
    YGR_Unit array_value;  
    /*  Integer identifying type of square (number
        returned by type function, e.g. texture
        index).*/ 
    YGR_Unit type;
    /*  Holds value of door roll. */
    YGR_Unit doorRoll;
    /*  Direction of hit. The convention for angle units is explained above. */
    u8       direction;    
} 
RENDER_HitResult;

/*  Holds an information about a single rendered pixel (for a pixel function
    that works as a fragment shader).
*/
typedef struct
{
    RENDER_HitResult  hit;         // < Corresponding ray hit.
    YGR_Vec2          position;    // < On-screen position.
    /*  Normalized (0 to YGR_UNITS_PER_SQUARE - 1)
        texture coordinates. */
    YGR_Vec2          texCoords; 
    YGR_Unit          depth;       // < Corrected depth.
    YGR_Unit          wall_height; // < Only for wall pixels, says its height.
    YGR_Unit          height;      // < World height (mostly for floor).
    u16              *destination; // < Destination address for pixel (precalculated).
    s8                shading;
    union {
        u8 flags;
        struct {
            bool
                is_wall   :1,  // < Whether the pixel is a wall or a floor/ceiling.
                is_floor  :1,  // < Whether the pixel is floor or ceiling.
                is_horizon:1;  // < If the pixel belongs to horizon segment.
        };
    };
} 
RENDER_PixelInfo;


/*******************************************************************************
    PUBLIC METHODS
*******************************************************************************/

/*  Function used to retrieve some information about cells of the rendered scene.
    It should return a characteristic of given square as an integer (e.g. square
    height, texture index, ...) - between squares that return different numbers
    there is considered to be a collision.

    This function should be as fast as possible as it will typically be called
    very often.
*/ 
typedef YGR_Unit (*RENDER_ArrayFunction)(s16 x, s16 y);
/*
    TODO: maybe array functions should be replaced by defines of funtion names
    like with pixelFunc? Could be more efficient than function pointers.
*/

/*  Function that renders a single pixel at the display. It is handed an info
    about the pixel it should kz;;draw.

    This function should be as fast as possible as it will typically be called
    very often.
*/
typedef void (*RENDER_PixelFunction)(RENDER_PixelInfo *info);
typedef void (*RENDER_ColumnFunction)(
    RENDER_HitResult *hits, 
    u16               hit_count, 
    u16               x,
    YGR_Ray        ray
);


/*  Simple-interface function to cast a single ray.

    @return          The first collision result.
*/
RENDER_HitResult 
RENDER_castRay(YGR_Ray ray, RENDER_ArrayFunction array_func);

/*  Casts a 3D ray in 3D environment with floor and optional ceiling
    (ceilingHeightFunc can be 0). This can be useful for hitscan shooting,
    visibility checking etc.

    @return normalized ditance (0 to YGR_UNITS_PER_SQUARE) along the ray at which
        the environment was hit, YGR_UNITS_PER_SQUARE means nothing was hit
*/
YGR_Unit 
RENDER_castRay3D(
    YGR_Vec2       pos1, 
    YGR_Unit           height1, 
    YGR_Vec2       pos2, 
    YGR_Unit           height2,
    RENDER_ArrayFunction  floor_height_func, 
    RENDER_ArrayFunction  ceiling_height_func,
    YGR_RayConstraints constraints
);

/* Maps a single point in the world to the screen (2D position + depth).
*/
RENDER_PixelInfo 
RENDER_mapToScreen(
    YGR_Vec2 world_position, 
    YGR_Unit     height,
    YGR_Camera   camera
);

/*  Casts a single ray and returns a list of collisions.

    @param ray ray to be cast, if RENDER_RECTILINEAR != 0 then the computed hit
        distance is divided by the ray direction vector length (to correct
        the fish eye effect)
    @param arrayFunc function that will be used to determine collisions (hits)
        with the ray (squares for which this function returns different values
        are considered to have a collision between them), this will typically
        be a function returning floor height
    @param typeFunc optional (can be 0) function - if provided, it will be used
        to mark the hit result with the number returned by this function
        (it can be e.g. a texture index)
    @param hitResults array in which the hit results will be stored (has to be
        preallocated with at space for at least as many hit results as
        maxHits specified with the constraints parameter)
    @param hitResultsLen in this variable the number of hit results will be
        returned
    @param constraints specifies constraints for the ray cast
*/
void 
RENDER_castRayMultiHit(
    YGR_Ray                ray, 
    RENDER_ArrayFunction   type_func, 
    RENDER_HitResult      *hit_results,
    u16                   *hit_results_len, 
    YGR_RayConstraints     constraints
);

///< Computes the change in size of an object due to perspective (vertical FOV).
YGR_Unit 
RENDER_perspectiveScaleVertical(         YGR_Unit original_size, YGR_Unit distance);

YGR_Unit 
RENDER_perspectiveScaleVerticalInverse(  YGR_Unit original_size, YGR_Unit scaled_size);

YGR_Unit
RENDER_perspectiveScaleHorizontal(       YGR_Unit original_size, YGR_Unit distance);

YGR_Unit 
RENDER_perspectiveScaleHorizontalInverse(YGR_Unit original_size, YGR_Unit scaled_size);


/*  Renders given camera view, with help of provided functions. This function is
    simpler and faster than RENDER_renderComplex(...) and is meant to be rendering
    flat levels.

    function rendering summary:
        - performance:            faster
        - accuracy:               lower
        - wall textures:          yes
        - different wall heights: yes
        - floor/ceiling textures: yes (only floor, you can mirror it for ceiling)
        - floor geometry:         no (just flat floor, with depth information)
        - ceiling geometry:       no (just flat ceiling, with depth information)
        - rolling door:           yes
        - camera shearing:        no
        - rendering order:        left-to-right, top-to-bottom

    Additionally this function supports rendering rolling doors.

    This function should render each screen pixel exactly once.

    @param rollFunc function that for given square says its door roll in
        YGR_Units (0 = no roll, YGR_UNITS_PER_SQUARE = full roll right,
        -YGR_UNITS_PER_SQUARE = full roll left), can be zero (no rolling door,
        rendering should also be faster as fewer intersections will be tested)
*/
void 
RENDER_render(
    YGR_Camera         *cam, 
    YGR_RayConstraints  constraints
);

/*  Function that moves given camera and makes it collide with walls and
    potentially also floor and ceilings. It's meant to help implement player
    movement.

    @param camera camera to move
    @param planeOffset offset to move the camera in
    @param heightOffset height offset to move the camera in
    @param floorHeightFunc function used to retrieve the floor height
    @param ceilingHeightFunc function for retrieving ceiling height, can be 0
        (camera won't collide with ceiling)
    @param computeHeight whether to compute height - if false (0), floor and
        ceiling functions won't be used and the camera will
        only collide horizontally with walls (good for simpler
        game, also faster)
    @param force if true, forces to recompute collision even if position doesn't
        change
*/
void 
RENDER_moveCameraWithCollision(
    YGR_Camera           *camera, 
    YGR_Vec2              planeOffset,
    YGR_Unit              heightOffset, 
    RENDER_ArrayFunction  floorHeightFunc,
    RENDER_ArrayFunction  ceilingHeightFunc, 
    s8                    compute_height, 
    s8                    force
);

void
RENDER_init(void);
void
RENDER_flip(void);
u32 
RENDER_getFrame(void);
u16 *
RENDER_getDrawBuffer(void);
void 
RENDER_plot(u8 x, u8 y, u8 color);


#endif /* YGR_RENDER_H */
