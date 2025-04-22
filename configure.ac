AC_PREREQ([2.64])

m4_define([wayland_eglstream_major_version], [1])
m4_define([wayland_eglstream_minor_version], [1])
m4_define([wayland_eglstream_micro_version], [20])
m4_define([wayland_eglstream_version],
          [wayland_eglstream_major_version.wayland_eglstream_minor_version.wayland_eglstream_micro_version])

AC_INIT([wayland-eglstream],
        [wayland_eglstream_version],
        [mvicomoya@nvidia.com])

AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([build])
AC_CONFIG_SRCDIR([config.h.in])
AC_CONFIG_HEADERS([config.h])

AC_GNU_SOURCE

AC_SUBST([WAYLAND_EXTERNAL_MAJOR_VERSION], [wayland_eglstream_major_version])
AC_SUBST([WAYLAND_EXTERNAL_MINOR_VERSION], [wayland_eglstream_minor_version])
AC_SUBST([WAYLAND_EXTERNAL_MICRO_VERSION], [wayland_eglstream_micro_version])
AC_SUBST([WAYLAND_EXTERNAL_VERSION], [wayland_eglstream_version])

AC_SUBST([EGL_EXTERNAL_PLATFORM_MIN_VERSION], [${WAYLAND_EXTERNAL_MAJOR_VERSION}.${WAYLAND_EXTERNAL_MINOR_VERSION}])
AC_SUBST([EGL_EXTERNAL_PLATFORM_MAX_VERSION], [$(($WAYLAND_EXTERNAL_MAJOR_VERSION + 1))])

# Add an --enable-debug option
AX_CHECK_ENABLE_DEBUG(no, DEBUG)

AC_USE_SYSTEM_EXTENSIONS

AM_INIT_AUTOMAKE([1.11 foreign subdir-objects])

AM_SILENT_RULES([yes])

PKG_PROG_PKG_CONFIG()

# Checks for programs.
AC_PROG_CC
AC_PROG_CXX
AM_PROG_AS
AC_PROG_LIBTOOL

AC_ARG_VAR([WAYLAND_SCANNER], [The wayland-scanner executable])
AC_PATH_PROG([WAYLAND_SCANNER], [wayland-scanner])

# User didn't specify wayland-scanner location manually, so find it ourselves
if test x$WAYLAND_SCANNER = x; then
    PKG_CHECK_MODULES(WAYLAND_SCANNER, [wayland-scanner])
    WAYLAND_SCANNER=`$PKG_CONFIG --variable=wayland_scanner wayland-scanner`
fi
AM_CONDITIONAL([WAYLAND_SCANNER_HAS_PRIVATE_CODE],
               [test x$WAYLAND_SCANNER = x`$PKG_CONFIG --variable=wayland_scanner "wayland-scanner >= 1.14.91"`])

# Check for protocols.
PKG_CHECK_MODULES(WAYLAND_PROTOCOLS, [wayland-protocols >= 1.8])
AC_SUBST(WAYLAND_PROTOCOLS_DATADIR, `$PKG_CONFIG --variable=pkgdatadir wayland-protocols`)

# Initialize libtool
LT_PREREQ([2.2])
LT_INIT

# Checks for libraries.
AX_PTHREAD()
AC_CHECK_LIB([dl], [dlsym],
             [],
             [AC_MSG_ERROR("dlsym is needed to compile wayland-egldisplay")])
PKG_CHECK_MODULES([EGL_HEADERS], [egl >= 1.5 egl < 2])
PKG_CHECK_MODULES([EGL_EXTERNAL_PLATFORM], [eglexternalplatform >= ${EGL_EXTERNAL_PLATFORM_MIN_VERSION} eglexternalplatform < ${EGL_EXTERNAL_PLATFORM_MAX_VERSION}])
PKG_CHECK_MODULES([WAYLAND], [wayland-server wayland-client wayland-egl-backend >= 3])
PKG_CHECK_MODULES([LIBDRM], [libdrm])

# Checks for header files.
AC_CHECK_HEADERS([arpa/inet.h stddef.h stdint.h stdlib.h string.h sys/socket.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_INLINE
AC_TYPE_INT32_T
AC_TYPE_SIZE_T
AC_TYPE_UINT32_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_CHECK_FUNCS([getpagesize inet_ntoa memset socket strcasecmp strstr])

# See if the compiler supports the -fvisibility=hidden flag.
AX_CHECK_COMPILE_FLAG([-fvisibility=hidden],
      [COMPILER_FLAG_VISIBILITY_HIDDEN="-fvisibility=hidden"],
      [COMPILER_FLAG_VISIBILITY_HIDDEN=""])
AC_SUBST([COMPILER_FLAG_VISIBILITY_HIDDEN])

# See if the linker supports the --no-undefined flag.
AX_CHECK_LINK_FLAG([-Xlinker --no-undefined],
      [LINKER_FLAG_NO_UNDEFINED="-Xlinker --no-undefined"],
      [LINKER_FLAG_NO_UNDEFINED=""])
AC_SUBST([LINKER_FLAG_NO_UNDEFINED])

# Default CFLAGS
CFLAGS="$CFLAGS -Wall -Werror -include config.h"

PKG_NOARCH_INSTALLDIR

AC_CONFIG_FILES([
    wayland-eglstream.pc
    wayland-eglstream-protocols.pc
    Makefile
    ])
AC_OUTPUT

AC_MSG_RESULT([
    Version                 ${WAYLAND_EXTERNAL_VERSION}
    Prefix                  ${prefix}
    ])
