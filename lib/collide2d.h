/*
 * collide2d.h - 2D collision detection on top of math2d.h
 *
 * Narrowphase shape tests, contact manifolds, and raycasts.
 * Broadphase (AABB culling) uses the aabb functions in math2d.h.
 *
 * Conventions:
 *   - All shapes are defined in local space; pass an xform2 to place
 *     them in the world. Circle/capsule centers are local coordinates.
 *   - Contact normals point from shape A toward shape B.
 *   - Polygons must be convex, wound counter-clockwise.
 *   - separation < 0 means overlapping (penetration depth = -separation).
 */

#ifndef COLLIDE2D_H
#define COLLIDE2D_H

#include "math2d.h"
#include <stdbool.h>

#define COLLIDE2D_MAX_POLY_VERTS 8
#define COLLIDE2D_MAX_CONTACTS   2

/* -------------------------------------------------------------------------
 * Shapes
 * ------------------------------------------------------------------------- */

/* Segment with radius (stadium). The standard character shape. */
typedef struct capsule {
   vec2 a, b;
   float radius;
} capsule;

/* Convex polygon, CCW winding. Precomputed edge normals. */
typedef struct polygon {
   vec2 verts[COLLIDE2D_MAX_POLY_VERTS];
   vec2 normals[COLLIDE2D_MAX_POLY_VERTS];
   int count;
} polygon;

/* One-sided edge for static terrain (collides from the left of a -> b). */
typedef struct segment {
   vec2 a, b;
} segment;

/* Polygon constructors (compute normals, validate winding) */
polygon polygon_make(const vec2 *verts, int count);
polygon polygon_box(vec2 half_extents); /* centered at origin */
polygon polygon_offset_box(vec2 half_extents, xform2 t);

/* -------------------------------------------------------------------------
 * Contact manifold
 * ------------------------------------------------------------------------- */

typedef struct contact {
   vec2 point;       /* world space contact point */
   float separation; /* < 0 when penetrating */
} contact;

typedef struct manifold {
   vec2 normal; /* world space, from A toward B (zero if no contact) */
   contact contacts[COLLIDE2D_MAX_CONTACTS];
   int count; /* 0 = no collision */
} manifold;

/* -------------------------------------------------------------------------
 * Narrowphase: shape vs shape
 * Each returns a manifold; manifold.count == 0 means no contact.
 * ------------------------------------------------------------------------- */

manifold collide_circles(circle a, xform2 xa, circle b, xform2 xb);
manifold collide_capsules(capsule a, xform2 xa, capsule b, xform2 xb);
manifold collide_polygons(polygon a, xform2 xa, polygon b, xform2 xb);

manifold collide_circle_capsule(circle a, xform2 xa, capsule b, xform2 xb);
manifold collide_circle_polygon(circle a, xform2 xa, polygon b, xform2 xb);
manifold collide_capsule_polygon(capsule a, xform2 xa, polygon b, xform2 xb);

manifold collide_segment_circle(segment a, xform2 xa, circle b, xform2 xb);
manifold collide_segment_capsule(segment a, xform2 xa, capsule b, xform2 xb);
manifold collide_segment_polygon(segment a, xform2 xa, polygon b, xform2 xb);

/* -------------------------------------------------------------------------
 * Boolean overlap tests (cheaper when you don't need contact info)
 * ------------------------------------------------------------------------- */

bool overlap_circles(circle a, xform2 xa, circle b, xform2 xb);
bool overlap_capsules(capsule a, xform2 xa, capsule b, xform2 xb);
bool overlap_polygons(polygon a, xform2 xa, polygon b, xform2 xb);
bool overlap_circle_polygon(circle a, xform2 xa, polygon b, xform2 xb);

/* -------------------------------------------------------------------------
 * Point queries
 * ------------------------------------------------------------------------- */

bool point_in_capsule(vec2 p, capsule c, xform2 t);
bool point_in_polygon(vec2 p, polygon poly, xform2 t);

/* -------------------------------------------------------------------------
 * Raycasts
 * ------------------------------------------------------------------------- */

typedef struct ray {
   vec2 origin;
   vec2 dir; /* must be unit length */
   float max_dist;
} ray;

typedef struct ray_hit {
   bool hit;
   vec2 point;  /* world space hit point */
   vec2 normal; /* surface normal at hit */
   float dist;  /* distance along ray */
} ray_hit;

ray_hit raycast_circle(ray r, circle c, xform2 t);
ray_hit raycast_capsule(ray r, capsule c, xform2 t);
ray_hit raycast_polygon(ray r, polygon p, xform2 t);
ray_hit raycast_segment(ray r, segment s, xform2 t);
ray_hit raycast_aabb(ray r, aabb b); /* aabb is already world space */

/* -------------------------------------------------------------------------
 * Bounds (for broadphase; world-space AABB of a placed shape)
 * ------------------------------------------------------------------------- */

aabb capsule_aabb(capsule c, xform2 t);
aabb polygon_aabb(polygon p, xform2 t);
aabb circle_aabb_xf(circle c, xform2 t);
aabb segment_aabb(segment s, xform2 t);

#endif /* COLLIDE2D_H */
