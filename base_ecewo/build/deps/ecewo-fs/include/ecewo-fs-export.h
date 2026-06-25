
#ifndef ECEWO_FS_EXPORT_H
#define ECEWO_FS_EXPORT_H

#ifdef ECEWO_FS_STATIC_DEFINE
#  define ECEWO_FS_EXPORT
#  define ECEWO_FS_NO_EXPORT
#else
#  ifndef ECEWO_FS_EXPORT
#    ifdef ecewo_fs_EXPORTS
        /* We are building this library */
#      define ECEWO_FS_EXPORT 
#    else
        /* We are using this library */
#      define ECEWO_FS_EXPORT 
#    endif
#  endif

#  ifndef ECEWO_FS_NO_EXPORT
#    define ECEWO_FS_NO_EXPORT 
#  endif
#endif

#ifndef ECEWO_FS_DEPRECATED
#  define ECEWO_FS_DEPRECATED __attribute__ ((__deprecated__))
#endif

#ifndef ECEWO_FS_DEPRECATED_EXPORT
#  define ECEWO_FS_DEPRECATED_EXPORT ECEWO_FS_EXPORT ECEWO_FS_DEPRECATED
#endif

#ifndef ECEWO_FS_DEPRECATED_NO_EXPORT
#  define ECEWO_FS_DEPRECATED_NO_EXPORT ECEWO_FS_NO_EXPORT ECEWO_FS_DEPRECATED
#endif

/* NOLINTNEXTLINE(readability-avoid-unconditional-preprocessor-if) */
#if 0 /* DEFINE_NO_DEPRECATED */
#  ifndef ECEWO_FS_NO_DEPRECATED
#    define ECEWO_FS_NO_DEPRECATED
#  endif
#endif

#endif /* ECEWO_FS_EXPORT_H */
