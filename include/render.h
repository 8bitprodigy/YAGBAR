#ifndef YAGBAR_RENDER_H
#define YAGBAR_RENDER_H


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

#ifndef RENDER_TEXTURE_INTERPOLATION_SCALE
    /*  This says scaling of fixed
        point vertical texture coord
        computation. This should be power
        of two! Higher number can look more 
        accurate but may cause overflow. */
    #define RENDER_TEXTURE_INTERPOLATION_SCALE 1024 
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
    /*  Distance to the hit position, or -1 if no collision happened. If 
        RENDER_RECTILINEAR != 0, then the distance is perpendicular to the 
        projection plane (fish eye correction), otherwise it is the straight 
        distance to the ray start position. */
    YAGBAR_Unit     distance; 
    /*  Direction of hit. The convention for angle units is explained above. */
    u8              direction;    
    /*  Normalized (0 to YAGBAR_UNITS_PER_SQUARE - 1) texture coordinate 
        (horizontal). */
    YAGBAR_Unit     texture_coord; 
    /*  Collided square coordinates. */
    YAGBAR_Vec2 square;   
    /*  Exact collision position in YAGBAR_Units. */
    YAGBAR_Vec2 position;  
    /*  Value returned by array function (most often this will be the floor 
        height). */   
    YAGBAR_Unit     array_value;  
    /*  Integer identifying type of square (number
        returned by type function, e.g. texture
        index).*/ 
    YAGBAR_Unit     type;
    /*  Holds value of door roll. */
    YAGBAR_Unit     doorRoll;
} 
RENDER_HitResult;

/*  Holds an information about a single rendered pixel (for a pixel function
    that works as a fragment shader).
*/
typedef struct
{
    YAGBAR_Vec2  position;    // < On-screen position.
    s8               is_wall;     // < Whether the pixel is a wall or a floor/ceiling.
    s8               is_floor;    // < Whether the pixel is floor or ceiling.
    s8               is_horizon;  // < If the pixel belongs to horizon segment.
    YAGBAR_Unit      depth;       // < Corrected depth.
    YAGBAR_Unit      wall_height; // < Only for wall pixels, says its height.
    YAGBAR_Unit      height;      // < World height (mostly for floor).
    RENDER_HitResult hit;         // < Corresponding ray hit.
    /*  Normalized (0 to YAGBAR_UNITS_PER_SQUARE - 1)
        texture coordinates. */
    YAGBAR_Vec2  texCoords; 
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
typedef YAGBAR_Unit (*RENDER_ArrayFunction)(s16 x, s16 y);
/*
    TODO: maybe array functions should be replaced by defines of funtion names
    like with pixelFunc? Could be more efficient than function pointers.
*/

/*  Function that renders a single pixel at the display. It is handed an info
    about the pixel it should draw.

    This function should be as fast as possible as it will typically be called
    very often.
*/
typedef void (*RENDER_PixelFunction)(RENDER_PixelInfo *info);
typedef void (*RENDER_ColumnFunction)(
    RENDER_HitResult *hits, 
    u16               hit_count, 
    u16               x,
    YAGBAR_Ray        ray
);


IWRAM_CODE static inline void RENDER_PIXEL_FUNCTION(RENDER_PixelInfo *pixel);
IWRAM_CODE static inline void RENDER_COLUMN_FUNCTION(
    YAGBAR_Unit x, 
    YAGBAR_Unit y_start, 
    YAGBAR_Unit y_end, 
    u8          is_floor
);
IWRAM_CODE inline YAGBAR_Unit RENDER_RS_HEIGHT_FN(s16 x, s16 y);

/*  Simple-interface function to cast a single ray.

    @return          The first collision result.
*/
static inline
RENDER_HitResult 
RENDER_castRay(YAGBAR_Ray ray, RENDER_ArrayFunction array_func);

/*  Casts a 3D ray in 3D environment with floor and optional ceiling
    (ceilingHeightFunc can be 0). This can be useful for hitscan shooting,
    visibility checking etc.

    @return normalized ditance (0 to YAGBAR_UNITS_PER_SQUARE) along the ray at which
        the environment was hit, YAGBAR_UNITS_PER_SQUARE means nothing was hit
*/
YAGBAR_Unit 
RENDER_castRay3D(
    YAGBAR_Vec2       pos1, 
    YAGBAR_Unit           height1, 
    YAGBAR_Vec2       pos2, 
    YAGBAR_Unit           height2,
    RENDER_ArrayFunction  floor_height_func, 
    RENDER_ArrayFunction  ceiling_height_func,
    YAGBAR_RayConstraints constraints
);

/* Maps a single point in the world to the screen (2D position + depth).
*/
inline
RENDER_PixelInfo 
RENDER_mapToScreen(
    YAGBAR_Vec2 world_position, 
    YAGBAR_Unit     height,
    YAGBAR_Camera   camera
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
inline 
void 
RENDER_castRayMultiHit(
    YAGBAR_Ray             ray, 
    RENDER_ArrayFunction   array_func,
    RENDER_ArrayFunction   type_func, 
    RENDER_HitResult      *hit_results,
    u16                   *hit_results_len, 
    YAGBAR_RayConstraints  constraints
);

///< Computes the change in size of an object due to perspective (vertical FOV).
YAGBAR_Unit 
RENDER_perspectiveScaleVertical(         YAGBAR_Unit original_size, YAGBAR_Unit distance);

YAGBAR_Unit 
RENDER_perspectiveScaleVerticalInverse(  YAGBAR_Unit original_size, YAGBAR_Unit scaled_size);

YAGBAR_Unit
RENDER_perspectiveScaleHorizontal(       YAGBAR_Unit original_size, YAGBAR_Unit distance);

YAGBAR_Unit 
RENDER_perspectiveScaleHorizontalInverse(YAGBAR_Unit original_size, YAGBAR_Unit scaled_size);

/*  Casts rays for given camera view and for each hit calls a user provided
    function.
*/
void 
RENDER_castRaysMultiHit(
    YAGBAR_Camera         cam, 
    RENDER_ArrayFunction  array_func,
    RENDER_ArrayFunction  type_function, 
    RENDER_ColumnFunction column_func,
    YAGBAR_RayConstraints constraints
);


/*  Using provided functions, renders a complete complex (multilevel) camera
    view.

    This function should render each screen pixel exactly once.

    function rendering summary:
        - performance:            slower
        - accuracy:               higher
        - wall textures:          yes
        - different wall heights: yes
        - floor/ceiling textures: no
        - floor geometry:         yes, multilevel
        - ceiling geometry:       yes (optional), multilevel
        - rolling door:           no
        - camera shearing:        yes
        - rendering order:        left-to-right, not specifically ordered vertically

    @param cam camera whose view to render
    @param floorHeightFunc function that returns floor height (in YAGBAR_Units)
    @param ceilingHeightFunc same as floorHeightFunc but for ceiling, can also 
        be 0 (no ceiling will be rendered)
    @param typeFunction function that says a type of square (e.g. its texture
        index), can be 0 (no type in hit result)
    @param pixelFunc callback function to draw a single pixel on screen
    @param constraints constraints for each cast ray
*/
void 
RENDER_renderComplex(
    YAGBAR_Camera         cam, 
    RENDER_ArrayFunction  floor_height_func,
    RENDER_ArrayFunction  ceiling_height_func, 
    RENDER_ArrayFunction  type_function,
    YAGBAR_RayConstraints constraints
);

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
        YAGBAR_Units (0 = no roll, YAGBAR_UNITS_PER_SQUARE = full roll right,
        -YAGBAR_UNITS_PER_SQUARE = full roll left), can be zero (no rolling door,
        rendering should also be faster as fewer intersections will be tested)
*/
void 
RENDER_renderSimple(
  YAGBAR_Camera         cam, 
  RENDER_ArrayFunction  floorHeightFunc,
  RENDER_ArrayFunction  typeFunc, 
  RENDER_ArrayFunction  rollFunc,
  YAGBAR_RayConstraints constraints
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
    YAGBAR_Camera        *camera, 
    YAGBAR_Vec2       planeOffset,
    YAGBAR_Unit           heightOffset, 
    RENDER_ArrayFunction  floorHeightFunc,
    RENDER_ArrayFunction  ceilingHeightFunc, 
    s8                    compute_height, 
    s8                    force
);

void
RENDER_flip(void);
u32 
RENDER_getFrame(void);
u16 *
RENDER_getDrawBuffer(void);





#endif /* YAGBAR_RENDER_H */
