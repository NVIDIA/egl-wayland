
# Install libraries
lib_LTLIBRARIES = libnvidia-egl-wayland.la

# Include paths
libnvidia_egl_wayland_la_CFLAGS =       \
    -I$(top_srcdir)/wayland-egl         \
    -I$(top_srcdir)/include             \
    -I$(top_builddir)/wayland-eglstream

# Required library flags
libnvidia_egl_wayland_la_CFLAGS +=  \
    $(PTHREAD_CFLAGS)               \
    $(EGL_EXTERNAL_PLATFORM_CFLAGS) \
    $(WAYLAND_CFLAGS)

libnvidia_egl_wayland_la_LDFLAGS =                                                                                        \
    -shared                                                                                                               \
    -Wl,-Bsymbolic                                                                                                        \
    -ldl                                                                                                                  \
    $(WAYLAND_LIBS)                                                                                                       \
    -version-number $(WAYLAND_EXTERNAL_MAJOR_VERSION):$(WAYLAND_EXTERNAL_MINOR_VERSION):$(WAYLAND_EXTERNAL_MICRO_VERSION) \
    $(LINKER_FLAG_NO_UNDEFINED)

libnvidia_egl_wayland_la_SOURCES =                            \
    src/wayland-api-lock.c                                    \
    src/wayland-egldisplay.c                                  \
    src/wayland-eglstream.c                                   \
    src/wayland-eglstream-server.c                            \
    src/wayland-eglsurface.c                                  \
    src/wayland-eglswap.c                                     \
    src/wayland-eglutils.c                                    \
    src/wayland-eglhandle.c                                   \
    src/wayland-external-exports.c

libnvidia_egl_wayland_la_built_sources =                             \
    wayland-eglstream/wayland-eglstream-protocol.c                   \
    wayland-eglstream/wayland-eglstream-client-protocol.h            \
    wayland-eglstream/wayland-eglstream-server-protocol.h            \
    wayland-eglstream/wayland-eglstream-controller-protocol.c        \
    wayland-eglstream/wayland-eglstream-controller-client-protocol.h

nodist_libnvidia_egl_wayland_la_SOURCES = $(libnvidia_egl_wayland_la_built_sources)
CLEANFILES = $(libnvidia_egl_wayland_la_built_sources)

$(libnvidia_egl_wayland_la_SOURCES): $(libnvidia_egl_wayland_la_built_sources)

%-protocol.c : %.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) code < $< > $@

%-client-protocol.h : %.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) client-header < $< > $@

%-server-protocol.h : %.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) server-header < $< > $@
