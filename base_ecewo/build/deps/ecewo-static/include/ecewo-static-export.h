
#ifndef ECEWO_STATIC_EXPORT_H
#define ECEWO_STATIC_EXPORT_H

#ifdef ECEWO_STATIC_STATIC_DEFINE
#  define ECEWO_STATIC_EXPORT
#  define ECEWO_STATIC_NO_EXPORT
#else
#  ifndef ECEWO_STATIC_EXPORT
#    ifdef ecewo_static_EXPORTS
        /* We are building this library */
#      define ECEWO_STATIC_EXPORT 
#    else
        /* We are using this library */
#      define ECEWO_STATIC_EXPORT 
#    endif
#  endif

#  ifndef ECEWO_STATIC_NO_EXPORT
#    define ECEWO_STATIC_NO_EXPORT 
#  endif
#endif

#ifndef ECEWO_STATIC_DEPRECATED
#  define ECEWO_STATIC_DEPRECATED __attribute__ ((__deprecated__))
#endif

#ifndef ECEWO_STATIC_DEPRECATED_EXPORT
#  define ECEWO_STATIC_DEPRECATED_EXPORT ECEWO_STATIC_EXPORT ECEWO_STATIC_DEPRECATED
#endif

#ifndef ECEWO_STATIC_DEPRECATED_NO_EXPORT
#  define ECEWO_STATIC_DEPRECATED_NO_EXPORT ECEWO_STATIC_NO_EXPORT ECEWO_STATIC_DEPRECATED
#endif

/* NOLINTNEXTLINE(readability-avoid-unconditional-preprocessor-if) */
#if 0 /* DEFINE_NO_DEPRECATED */
#  ifndef ECEWO_STATIC_NO_DEPRECATED
#    define ECEWO_STATIC_NO_DEPRECATED
#  endif
#endif

#endif /* ECEWO_STATIC_EXPORT_H */
