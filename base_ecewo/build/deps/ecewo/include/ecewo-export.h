
#ifndef ECEWO_EXPORT_H
#define ECEWO_EXPORT_H

#ifdef ECEWO_STATIC_DEFINE
#  define ECEWO_EXPORT
#  define ECEWO_NO_EXPORT
#else
#  ifndef ECEWO_EXPORT
#    ifdef ecewo_EXPORTS
        /* We are building this library */
#      define ECEWO_EXPORT 
#    else
        /* We are using this library */
#      define ECEWO_EXPORT 
#    endif
#  endif

#  ifndef ECEWO_NO_EXPORT
#    define ECEWO_NO_EXPORT 
#  endif
#endif

#ifndef ECEWO_DEPRECATED
#  define ECEWO_DEPRECATED __attribute__ ((__deprecated__))
#endif

#ifndef ECEWO_DEPRECATED_EXPORT
#  define ECEWO_DEPRECATED_EXPORT ECEWO_EXPORT ECEWO_DEPRECATED
#endif

#ifndef ECEWO_DEPRECATED_NO_EXPORT
#  define ECEWO_DEPRECATED_NO_EXPORT ECEWO_NO_EXPORT ECEWO_DEPRECATED
#endif

/* NOLINTNEXTLINE(readability-avoid-unconditional-preprocessor-if) */
#if 0 /* DEFINE_NO_DEPRECATED */
#  ifndef ECEWO_NO_DEPRECATED
#    define ECEWO_NO_DEPRECATED
#  endif
#endif

#endif /* ECEWO_EXPORT_H */
