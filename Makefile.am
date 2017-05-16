
# Install libraries
lib_LTLIBRARIES = libnvidia-egl-wayland.la

# Include paths
libnvidia_egl_wayland_la_CFLAGS =     \
    -I$(top_srcdir)/wayland-egl       \
    -I$(top_srcdir)/wayland-eglstream \
    -I$(top_srcdir)/include

# Required library flags
libnvidia_egl_wayland_la_CFLAGS += \
    $(PTHREAD_CFLAGS)              \
    $(EGL_EXTERNAL_PLATFORM_CFLAGS)

libnvidia_egl_wayland_la_LDFLAGS =                                                                                        \
    -shared                                                                                                               \
    -Wl,-Bsymbolic                                                                                                        \
    -ldl                                                                                                                  \
    -lwayland-server                                                                                                      \
    -lwayland-client                                                                                                      \
    -version-number $(WAYLAND_EXTERNAL_MAJOR_VERSION):$(WAYLAND_EXTERNAL_MINOR_VERSION):$(WAYLAND_EXTERNAL_MICRO_VERSION) \
    $(LINKER_FLAG_NO_UNDEFINED)

libnvidia_egl_wayland_la_SOURCES =                            \
    wayland-eglstream/wayland-eglstream-protocol.c            \
    wayland-eglstream/wayland-eglstream-controller-protocol.c \
    src/wayland-api-lock.c                                    \
    src/wayland-egldisplay.c                                  \
    src/wayland-eglstream.c                                   \
    src/wayland-eglstream-server.c                            \
    src/wayland-eglsurface.c                                  \
    src/wayland-eglswap.c                                     \
    src/wayland-eglutils.c                                    \
    src/wayland-eglhandle.c                                   \
    src/wayland-external-exports.c
