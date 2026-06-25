// Generated from cmake/version.h.in by CMake. Do not edit.

#ifndef ECEWO_VERSION_H
#define ECEWO_VERSION_H

#define ECEWO_VERSION_MAJOR 4
#define ECEWO_VERSION_MINOR 1
#define ECEWO_VERSION_PATCH 0
#define ECEWO_VERSION_STRING "4.1.0"

/* Single integer for compile-time comparisons, e.g.
 *   #if ECEWO_VERSION >= ECEWO_VERSION_NUM(4, 1, 0)
 */
#define ECEWO_VERSION_NUM(major, minor, patch) \
  ((major) * 10000 + (minor) * 100 + (patch))

#define ECEWO_VERSION \
  ECEWO_VERSION_NUM(ECEWO_VERSION_MAJOR, ECEWO_VERSION_MINOR, ECEWO_VERSION_PATCH)

#endif /* ECEWO_VERSION_H */
