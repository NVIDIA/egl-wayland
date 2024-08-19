
# Install libraries
lib_LTLIBRARIES = libnvidia-egl-wayland.la

# Include paths
libnvidia_egl_wayland_la_CFLAGS =       \
    -I$(top_srcdir)/wayland-egl         \
    -I$(top_srcdir)/include             \
    -I$(top_builddir)/wayland-eglstream \
    -I$(top_builddir)/wayland-drm

# Required library flags
libnvidia_egl_wayland_la_CFLAGS +=      \
    $(PTHREAD_CFLAGS)                   \
    $(EGL_CFLAGS)                       \
    $(EGL_EXTERNAL_PLATFORM_CFLAGS)     \
    $(WAYLAND_CFLAGS)                   \
    $(LIBDRM_CFLAGS)                    \
    $(COMPILER_FLAG_VISIBILITY_HIDDEN)

# Make sure we don't use deprecated stuff
libnvidia_egl_wayland_la_CFLAGS += \
    -DWL_HIDE_DEPRECATED

libnvidia_egl_wayland_la_LDFLAGS =                                                                                        \
    -shared                                                                                                               \
    -Wl,-Bsymbolic                                                                                                        \
    -ldl                                                                                                                  \
    $(WAYLAND_LIBS)                                                                                                       \
    $(LIBDRM_LIBS)                                                                                                        \
    -version-number $(WAYLAND_EXTERNAL_MAJOR_VERSION):$(WAYLAND_EXTERNAL_MINOR_VERSION):$(WAYLAND_EXTERNAL_MICRO_VERSION) \
    $(LINKER_FLAG_NO_UNDEFINED)

libnvidia_egl_wayland_la_SOURCES =                            \
    src/wayland-thread.c                                      \
    src/wayland-egldevice.c                                   \
    src/wayland-egldisplay.c                                  \
    src/wayland-eglstream.c                                   \
    src/wayland-eglstream-server.c                            \
    src/wayland-eglsurface.c                                  \
    src/wayland-eglswap.c                                     \
    src/wayland-eglutils.c                                    \
    src/wayland-eglhandle.c                                   \
    src/wayland-drm.c                                         \
    src/wayland-external-exports.c

libnvidia_egl_wayland_la_SOURCES += \
    include/wayland-drm.h                 \
    include/wayland-egldevice.h           \
    include/wayland-egldisplay.h          \
    include/wayland-eglhandle.h           \
    include/wayland-eglstream.h           \
    include/wayland-eglstream-server.h    \
    include/wayland-eglsurface.h          \
    include/wayland-eglsurface-internal.h \
    include/wayland-eglswap.h             \
    include/wayland-eglutils.h            \
    include/wayland-external-exports.h    \
    include/wayland-thread.h              \
    wayland-egl/wayland-egl-ext.h

libnvidia_egl_wayland_la_built_public_protocols =             \
    wayland-eglstream/wayland-eglstream-controller-protocol.c \
    wayland-drm/wayland-drm-protocol.c

libnvidia_egl_wayland_la_built_private_protocols =            \
    wayland-eglstream/wayland-eglstream-protocol.c

libnvidia_egl_wayland_la_built_client_headers =               \
    wayland-eglstream/wayland-eglstream-client-protocol.h     \
    wayland-eglstream/wayland-eglstream-controller-client-protocol.h \
    wayland-drm/wayland-drm-client-protocol.h

libnvidia_egl_wayland_la_built_server_headers =               \
    wayland-eglstream/wayland-eglstream-server-protocol.h     \
    wayland-drm/wayland-drm-server-protocol.h

libnvidia_egl_wayland_la_dmabuf_built_client_headers =        \
    linux-dmabuf-unstable-v1-client-protocol.h

libnvidia_egl_wayland_la_dmabuf_built_private_protocols =     \
    linux-dmabuf-unstable-v1-protocol.c

libnvidia_egl_wayland_la_drm_syncobj_built_client_headers =       \
    linux-drm-syncobj-v1-client-protocol.h

libnvidia_egl_wayland_la_drm_syncobj_built_private_protocols =    \
    linux-drm-syncobj-v1-protocol.c

libnvidia_egl_wayland_la_presentation_time_built_client_headers = \
    presentation-time-client-protocol.h

libnvidia_egl_wayland_la_presentation_time_private_protocols =    \
    presentation-time-protocol.c

libnvidia_egl_wayland_la_built_sources =                               \
    $(libnvidia_egl_wayland_la_built_public_protocols)                 \
    $(libnvidia_egl_wayland_la_built_private_protocols)                \
    $(libnvidia_egl_wayland_la_built_client_headers)                   \
    $(libnvidia_egl_wayland_la_built_server_headers)                   \
    $(libnvidia_egl_wayland_la_dmabuf_built_client_headers)            \
    $(libnvidia_egl_wayland_la_dmabuf_built_private_protocols)         \
    $(libnvidia_egl_wayland_la_drm_syncobj_built_client_headers)       \
    $(libnvidia_egl_wayland_la_drm_syncobj_built_private_protocols)    \
    $(libnvidia_egl_wayland_la_presentation_time_built_client_headers) \
    $(libnvidia_egl_wayland_la_presentation_time_private_protocols)

nodist_libnvidia_egl_wayland_la_SOURCES = $(libnvidia_egl_wayland_la_built_sources)

dist_pkgdata_DATA =                                    \
    wayland-eglstream/wayland-eglstream.xml            \
    wayland-eglstream/wayland-eglstream-controller.xml \
    wayland-drm/wayland-drm.xml

wayland_eglstream_pkgconfig_files = \
    wayland-eglstream.pc            \
    wayland-eglstream-protocols.pc

noarch_pkgconfig_DATA = $(wayland_eglstream_pkgconfig_files)

CLEANFILES =                                  \
    $(libnvidia_egl_wayland_la_built_sources) \
    $(wayland_eglstream_pkgconfig_files)

$(libnvidia_egl_wayland_la_SOURCES): $(libnvidia_egl_wayland_la_built_sources)

if WAYLAND_SCANNER_HAS_PRIVATE_CODE
    WAYLAND_PUBLIC_CODEGEN = public-code
    WAYLAND_PRIVATE_CODEGEN = private-code
else
    WAYLAND_PUBLIC_CODEGEN = code
    WAYLAND_PRIVATE_CODEGEN = code
endif

$(libnvidia_egl_wayland_la_dmabuf_built_private_protocols):%-protocol.c : $(WAYLAND_PROTOCOLS_DATADIR)/unstable/linux-dmabuf/%.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) $(WAYLAND_PRIVATE_CODEGEN) < $< > $@

$(libnvidia_egl_wayland_la_dmabuf_built_client_headers):%-client-protocol.h : $(WAYLAND_PROTOCOLS_DATADIR)/unstable/linux-dmabuf/%.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) client-header < $< > $@

$(libnvidia_egl_wayland_la_drm_syncobj_built_private_protocols):%-protocol.c : $(WAYLAND_PROTOCOLS_DATADIR)/staging/linux-drm-syncobj/%.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) $(WAYLAND_PRIVATE_CODEGEN) < $< > $@

$(libnvidia_egl_wayland_la_drm_syncobj_built_client_headers):%-client-protocol.h : $(WAYLAND_PROTOCOLS_DATADIR)/staging/linux-drm-syncobj/%.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) client-header < $< > $@

$(libnvidia_egl_wayland_la_presentation_time_private_protocols):%-protocol.c : $(WAYLAND_PROTOCOLS_DATADIR)/stable/presentation-time/%.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) $(WAYLAND_PRIVATE_CODEGEN) < $< > $@

$(libnvidia_egl_wayland_la_presentation_time_built_client_headers):%-client-protocol.h : $(WAYLAND_PROTOCOLS_DATADIR)/stable/presentation-time/%.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) client-header < $< > $@

$(libnvidia_egl_wayland_la_built_public_protocols):%-protocol.c : %.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) $(WAYLAND_PUBLIC_CODEGEN) < $< > $@

$(libnvidia_egl_wayland_la_built_private_protocols):%-protocol.c : %.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) $(WAYLAND_PRIVATE_CODEGEN) < $< > $@

$(libnvidia_egl_wayland_la_built_client_headers):%-client-protocol.h : %.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) client-header < $< > $@

$(libnvidia_egl_wayland_la_built_server_headers):%-server-protocol.h : %.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) server-header < $< > $@
