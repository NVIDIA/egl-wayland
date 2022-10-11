/*
 * Copyright (c) 2014-2022, NVIDIA CORPORATION. All rights reserved.
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

#ifndef WAYLAND_EGLSURFACE_H
#define WAYLAND_EGLSURFACE_H

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <pthread.h>
#include <wayland-client.h>
#include "wayland-egldisplay.h"
#include "wayland-eglutils.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct WlEglSurfaceRec WlEglSurface;

WL_EXPORT
EGLStreamKHR wlEglGetSurfaceStreamExport(WlEglSurface *surface);

WL_EXPORT
WlEglSurface *wlEglCreateSurfaceExport(EGLDisplay dpy,
                                       int width,
                                       int height,
                                       struct wl_surface *native_surface,
                                       int fifo_length);

WL_EXPORT
WlEglSurface *wlEglCreateSurfaceExport2(EGLDisplay dpy,
                                        int width,
                                        int height,
                                        struct wl_surface *native_surface,
                                        int fifo_length,
                                        int (*present_update_callback)(void*, uint64_t, int),
                                        const EGLAttrib *attribs);

WL_EXPORT
int wlEglWaitAllPresentationFeedbacksExport(WlEglSurface *surface);

WL_EXPORT
int wlEglProcessPresentationFeedbacksExport(WlEglSurface *surface);

#ifdef __cplusplus
}
#endif

#endif
