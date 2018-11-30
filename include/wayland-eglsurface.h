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

typedef struct WlEglSurfaceCtxRec {
    EGLBoolean    isOffscreen;
    EGLSurface    eglSurface;
    EGLStreamKHR  eglStream;
    void         *wlStreamResource;
    EGLBoolean    isAttached;

    int          useDamageThread;
    pthread_t    damageThreadId;
    EGLSyncKHR   damageThreadSync;
    int          damageThreadFlush;
    int          damageThreadShutdown;
    EGLuint64KHR framesProduced;
    EGLuint64KHR framesFinished;
    EGLuint64KHR framesProcessed;

    struct wl_list link;
} WlEglSurfaceCtx;

typedef struct WlEglSurfaceRec {
    WlEglDisplay *wlEglDpy;
    EGLConfig     eglConfig;
    EGLint       *attribs;

    struct wl_egl_window *wlEglWin;
    long int              wlEglWinVer;
    struct wl_surface    *wlSurface;
    int                   width, height;
    int                   dx,    dy;

    WlEglSurfaceCtx ctx;
    struct wl_list  oldCtxList;

    EGLint swapInterval;

    struct wl_callback    *throttleCallback;

    struct wl_list link;

    EGLBoolean isSurfaceProducer;
} WlEglSurface;

extern struct wl_list wlEglSurfaceList;

WL_EXPORT
EGLBoolean wlEglInitializeSurfaceExport(WlEglSurface *surface);

EGLSurface wlEglCreatePlatformWindowSurfaceHook(EGLDisplay dpy,
                                                EGLConfig config,
                                                void *nativeWin,
                                                const EGLAttrib *attribs);
EGLSurface wlEglCreatePlatformPixmapSurfaceHook(EGLDisplay dpy,
                                                EGLConfig config,
                                                void *nativePixmap,
                                                const EGLAttrib *attribs);
EGLSurface wlEglCreatePbufferSurfaceHook(EGLDisplay dpy,
                                         EGLConfig config,
                                         const EGLint *attribs);
EGLSurface wlEglCreateStreamProducerSurfaceHook(EGLDisplay dpy,
                                                EGLConfig config,
                                                EGLStreamKHR stream,
                                                const EGLint *attribs);
EGLBoolean wlEglDestroySurfaceHook(EGLDisplay dpy, EGLSurface eglSurface);
EGLBoolean wlEglDestroyAllSurfaces(WlEglDisplay *display);

EGLBoolean wlEglIsWaylandWindowValid(struct wl_egl_window *window);
EGLBoolean wlEglIsWlEglSurface(WlEglSurface *wlEglSurface);

EGLBoolean wlEglQueryNativeResourceHook(EGLDisplay dpy,
                                        void *nativeResource,
                                        EGLint attribute,
                                        int *value);

EGLBoolean wlEglSendDamageEvent(WlEglSurface *surface,
                                struct wl_event_queue *queue);

void wlEglCreateFrameSync(WlEglSurface *surface, struct wl_event_queue *queue);
EGLint wlEglWaitFrameSync(WlEglSurface *surface, struct wl_event_queue *queue);
#ifdef __cplusplus
}
#endif

#endif
