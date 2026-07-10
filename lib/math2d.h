/*
 * math2d.h - Generic 2D math for games (rendering + physics)
 *
 * Types and declarations only. Conventions:
 *   - Angles in radians, counter-clockwise positive, Y-up world space.
 *   - rot2/xform2 are for rigid bodies (no scale, cheap inverse).
 *   - mat2x3 is for rendering (supports scale/shear), row-major,
 *     layout: [ m00 m01 m02 ]   x' = m00*x + m01*y + m02
 *             [ m10 m11 m12 ]   y' = m10*x + m11*y + m12
 *   - vec2_norm on a zero-length vector returns (0,0), never NaN.
 */

#ifndef MATH2D_H
#define MATH2D_H

#include <stdbool.h>

/* -------------------------------------------------------------------------
 * Types
 * ------------------------------------------------------------------------- */

typedef union vec2 {
   struct {
      float x, y;
   };
   float v[2];
} vec2;

typedef union ivec2 {
   struct {
      int x, y;
   };
   int v[2];
} ivec2;

typedef vec2 point;

typedef struct line {
   point a, b;
} line;

typedef struct triangle {
   point a, b, c;
} triangle;

typedef struct circle {
   vec2 center;
   float radius;
} circle;

/* Rotation as a unit complex number (sin/cos pair).
 * Avoids trig calls in hot paths; composing = complex multiply. */
typedef struct rot2 {
   float s, c;
} rot2;

/* Rigid body transform: rotation + translation. No scale. */
typedef struct xform2 {
   vec2 p;
   rot2 q;
} xform2;

/* Affine transform for rendering: rotation/scale/shear + translation. */
typedef struct mat2x3 {
   float m[2][3];
} mat2x3;

/* Position + size rectangle (sprites, cameras, UI). */
typedef struct rect {
   vec2 pos;
   vec2 size;
} rect;

typedef struct irect {
   ivec2 pos;
   ivec2 size;
} irect;

/* Min/max bounding box (physics broadphase). */
typedef struct aabb {
   vec2 min;
   vec2 max;
} aabb;

/* Color, float per channel, 0.0 - 1.0. The main color type for math,
 * blending, and uniforms. Layout-compatible with sg_color. */
typedef union color {
   struct {
      float r, g, b, a;
   };
   float v[4];
} color;

/* Color, 8 bits per channel (vertex formats, compact storage). */
typedef union rgba8 {
   struct {
      unsigned char r, g, b, a;
   };
   unsigned char v[4];
} rgba8;

/* -------------------------------------------------------------------------
 * Scalar helpers
 * ------------------------------------------------------------------------- */

float clampf(float x, float lo, float hi);
float lerpf(float a, float b, float t);

/* -------------------------------------------------------------------------
 * vec2
 * ------------------------------------------------------------------------- */

vec2 vec2_add(vec2 a, vec2 b);
vec2 vec2_sub(vec2 a, vec2 b);
vec2 vec2_neg(vec2 a);
vec2 vec2_scale(vec2 a, float s);           /* a * s */
vec2 vec2_mul_add(vec2 a, vec2 b, float s); /* a + b*s (integrator step) */
vec2 vec2_mul(vec2 a, vec2 b);              /* component-wise (Hadamard) */

float vec2_dot(vec2 a, vec2 b);
float vec2_cross(vec2 a, vec2 b);    /* scalar z-component: a.x*b.y - a.y*b.x */
vec2 vec2_cross_sv(float s, vec2 v); /* angular vel x arm: (-s*v.y, s*v.x) */
vec2 vec2_perp(vec2 a);              /* rotate +90 deg: (-y, x) */

float vec2_len(vec2 a);
float vec2_len_sq(vec2 a);
float vec2_dist(vec2 a, vec2 b);
float vec2_dist_sq(vec2 a, vec2 b);
vec2 vec2_norm(vec2 a); /* returns (0,0) if len == 0 */

vec2 vec2_lerp(vec2 a, vec2 b, float t);
vec2 vec2_clamp(vec2 a, vec2 lo, vec2 hi);
vec2 vec2_min(vec2 a, vec2 b);
vec2 vec2_max(vec2 a, vec2 b);
vec2 vec2_abs(vec2 a);
vec2 vec2_reflect(vec2 v, vec2 n); /* n must be unit length */

float vec2_angle(vec2 a);            /* atan2(y, x) */
vec2 vec2_from_angle(float radians); /* (cos, sin), unit length */

/* -------------------------------------------------------------------------
 * ivec2 (grid coords, tile maps, pixel positions)
 * ------------------------------------------------------------------------- */

ivec2 ivec2_add(ivec2 a, ivec2 b);
ivec2 ivec2_sub(ivec2 a, ivec2 b);
ivec2 ivec2_neg(ivec2 a);
ivec2 ivec2_scale(ivec2 a, int s);
bool ivec2_eq(ivec2 a, ivec2 b);

vec2 ivec2_to_vec2(ivec2 a);
ivec2 vec2_to_ivec2(vec2 a);    /* truncates toward zero */
ivec2 vec2_floor_ivec2(vec2 a); /* floor (correct for negative tile coords) */

/* -------------------------------------------------------------------------
 * rot2
 * ------------------------------------------------------------------------- */

rot2 rot2_identity(void);
rot2 rot2_from_angle(float radians);
float rot2_angle(rot2 q);

rot2 rot2_mul(rot2 a, rot2 b);     /* compose: rotate by b then a */
rot2 rot2_inv_mul(rot2 a, rot2 b); /* inverse(a) * b */

vec2 rot2_rotate(rot2 q, vec2 v);
vec2 rot2_inv_rotate(rot2 q, vec2 v);

/* -------------------------------------------------------------------------
 * xform2 (rigid body: collision, local <-> world space)
 * ------------------------------------------------------------------------- */

xform2 xform2_identity(void);
xform2 xform2_mul(xform2 a, xform2 b);
xform2 xform2_inv_mul(xform2 a, xform2 b); /* b in a's local space */

vec2 xform2_point(xform2 t, vec2 p);     /* local -> world */
vec2 xform2_inv_point(xform2 t, vec2 p); /* world -> local */

/* -------------------------------------------------------------------------
 * mat2x3 (rendering transforms)
 * ------------------------------------------------------------------------- */

mat2x3 mat2x3_identity(void);
mat2x3 mat2x3_mul(mat2x3 a, mat2x3 b);

mat2x3 mat2x3_translate(vec2 t);
mat2x3 mat2x3_rotate(float radians);
mat2x3 mat2x3_rotate_at(float radians, vec2 pivot);
mat2x3 mat2x3_scale(vec2 s);
mat2x3 mat2x3_scale_at(vec2 s, vec2 pivot);

mat2x3 mat2x3_from_xform2(xform2 t);
mat2x3 mat2x3_ortho(float left, float right, float top,
                    float bottom); /* coords -> NDC */
mat2x3 mat2x3_invert(mat2x3 m);    /* undefined if determinant == 0 */
float mat2x3_det(mat2x3 m);

vec2 mat2x3_point(mat2x3 m, vec2 p);  /* full transform */
vec2 mat2x3_vector(mat2x3 m, vec2 v); /* rotation/scale only, no translation */
void mat2x3_points(mat2x3 m, vec2 *dst, const vec2 *src,
                   int count); /* batch vertex transform */

/* -------------------------------------------------------------------------
 * line / triangle
 * ------------------------------------------------------------------------- */

float line_len(line l);
vec2 line_dir(line l);                            /* normalized a -> b */
vec2 line_closest_point(line l, vec2 p);          /* closest point on segment */
bool line_intersect(line l1, line l2, vec2 *out); /* segment-segment */

float triangle_area(triangle t); /* signed: > 0 if CCW winding */
bool triangle_contains(triangle t, vec2 p);
aabb triangle_aabb(triangle t);

/* -------------------------------------------------------------------------
 * circle
 * ------------------------------------------------------------------------- */

bool circle_contains(circle c, vec2 p);
bool circle_overlaps(circle a, circle b);
bool circle_overlaps_aabb(circle c, aabb b);
vec2 circle_closest_point(circle c, vec2 p); /* closest point on circle edge */
aabb circle_aabb(circle c);

/* -------------------------------------------------------------------------
 * rect / aabb
 * ------------------------------------------------------------------------- */

bool rect_contains(rect r, vec2 p);
bool rect_overlaps(rect a, rect b);
vec2 rect_center(rect r);
aabb rect_to_aabb(rect r);
rect aabb_to_rect(aabb b);

aabb aabb_union(aabb a, aabb b);
bool aabb_contains(aabb a, aabb b); /* a fully contains b */
bool aabb_contains_point(aabb a, vec2 p);
bool aabb_overlaps(aabb a, aabb b);
aabb aabb_extend(aabb a, float margin); /* fatten for broadphase */
vec2 aabb_center(aabb a);
vec2 aabb_half_extents(aabb a);

/* -------------------------------------------------------------------------
 * color (float) / rgba8 (byte storage)
 * ------------------------------------------------------------------------- */

/* Constructors */
color color_rgba(float r, float g, float b, float a);
color color_rgb(float r, float g, float b); /* a = 1.0 */
rgba8 color_rgba8(unsigned char r, unsigned char g, unsigned char b,
                  unsigned char a);

/* Conversions */
color rgba8_to_color(rgba8 c);
rgba8 color_to_rgba8(color c); /* clamps each channel to 0-1 first */

/* Packed 32-bit, 0xRRGGBBAA (hex literals: rgba8_from_hex(0xFF8800FF)) */
rgba8 rgba8_from_hex(unsigned int hex);
unsigned int rgba8_to_hex(rgba8 c);
color color_from_hex(unsigned int hex);

/* Math (float only -- convert rgba8 up, operate, convert back) */
color color_lerp(color a, color b, float t);
color color_mul(color a, color b);   /* component-wise (modulate/tint) */
color color_scale(color c, float s); /* scales rgb and a */
color color_with_alpha(color c, float a);
color color_premultiply(color c); /* rgb *= a, for premultiplied blending */

/* Common constants */
color color_white(void);
color color_black(void);
color color_transparent(void);

#endif /* MATH2D_H */
