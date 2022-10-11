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

#ifndef WAYLAND_EGLSWAP_H
#define WAYLAND_EGLSWAP_H

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include "wayland-eglhandle.h"
#include "wayland-eglsurface.h"

#ifdef __cplusplus
extern "C" {
#endif

EGLBoolean wlEglSwapBuffersHook(EGLDisplay dpy, EGLSurface eglSurface);
EGLBoolean wlEglSwapBuffersWithDamageHook(EGLDisplay eglDisplay,
                                          EGLSurface eglSurface,
                                          EGLint *rects,
                                          EGLint n_rects);
EGLBoolean wlEglSwapIntervalHook(EGLDisplay eglDisplay, EGLint interval);

EGLint wlEglStreamSwapIntervalCallback(WlEglPlatformData *data,
                                       EGLStreamKHR stream,
                                       EGLint *interval);

WL_EXPORT
EGLBoolean wlEglPrePresentExport(WlEglSurface *surface);

WL_EXPORT
EGLBoolean wlEglPostPresentExport(WlEglSurface *surface);

WL_EXPORT
EGLBoolean wlEglPostPresentExport2(WlEglSurface *surface,
                                   uint64_t presentId,
                                   void *presentInfo);

#ifdef __cplusplus
}
#endif

#endif
