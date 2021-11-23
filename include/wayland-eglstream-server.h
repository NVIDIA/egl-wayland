/*
 * Copyright (c) 2014-2016, NVIDIA CORPORATION. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef WAYLAND_EGLSTREAM_SERVER_H
#define WAYLAND_EGLSTREAM_SERVER_H

#include <wayland-server-protocol.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include "wayland-eglhandle.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Forward declarations
 */
struct wl_eglstream_display;
struct wl_eglstream;


/*
 * wl_eglstream_display_bind()
 *
 * Creates and initializes a wl_eglstream_display connection associated to the
 * given wl_display and EGLDisplay.
 */
EGLBoolean
wl_eglstream_display_bind(WlEglPlatformData *data,
                          struct wl_display *wlDisplay,
                          EGLDisplay eglDisplay,
                          const char *exts,
                          const char *dev_name);

/*
 * wl_eglstream_display_unbind()
 *
 * Destroys the given wl_eglstream_display connection result of a previous
 * wl_eglstream_display_bind() call.
 */
void wl_eglstream_display_unbind(struct wl_eglstream_display *wlStreamDpy);

/*
 * wl_eglstream_display_get()
 *
 * Given an EGL display, returns its associated wl_eglstream_display connection.
 */
struct wl_eglstream_display* wl_eglstream_display_get(EGLDisplay eglDisplay);

/*
 * wl_eglstream_display_get_stream()
 *
 * Given a generic wl_resource, returns its associated wl_eglstream.
 */
struct wl_eglstream*
wl_eglstream_display_get_stream(struct wl_eglstream_display *wlStreamDpy,
                                struct wl_resource *resource);


/* wl_eglstream_display definition */
struct wl_eglstream_display {
    WlEglPlatformData *data;

    struct wl_global  *global;
    struct wl_display *wlDisplay;

    EGLDisplay eglDisplay;
    struct {
        int stream_attrib           : 1;
        int stream_cross_process_fd : 1;
        int stream_remote           : 1;
        int stream_socket           : 1;
        int stream_socket_inet      : 1;
        int stream_socket_unix      : 1;
        int stream_origin           : 1;
    } exts;

    struct {
        const char       *device_name;
        struct wl_global *global;
    } *drm;

    int caps_override               : 1;
    int supported_caps;

    struct wl_buffer_interface wl_eglstream_interface;

    struct wl_list link;
};

/* wl_eglstream definition */
struct wl_eglstream {
    struct wl_resource          *resource;
    struct wl_eglstream_display *wlStreamDpy;

    int width, height;

    EGLBoolean   fromFd;
    EGLBoolean   isInet;
    int          handle;
    EGLStreamKHR eglStream;

    /*
     * The following attribute encodes the default value for a
     * stream's image inversion relative to wayland protocol
     * convention. Vulkan apps will be set to 'true', while
     * OpenGL apps will be set to 'false'.
     * NOTE: EGL_NV_stream_origin is the authorative source of
     * truth regarding a stream's frame orientation and should be
     * queried for an accurate value. The following attribute is a
     * 'best guess' fallback mechanism which should only be used
     * when a query to EGL_NV_stream_origin fails.
     */
    EGLBoolean yInverted;
};

#ifdef __cplusplus
}
#endif

#endif
