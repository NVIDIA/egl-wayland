/*
 * Copyright (c) 2014-2019, NVIDIA CORPORATION. All rights reserved.
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

typedef struct WlEglStreamImageRec {
    /* Pointer back to the parent surface for use in Wayland callbacks */
    struct WlEglSurfaceRec *surface;

    /*
     * Use an individual mutex to guard access to each image's data. This avoids
     * sharing the surface lock between the app and buffer release event
     * threads, resulting in simplified lock management and smaller critical
     * sections.
     */
    pthread_mutex_t         mutex;

    EGLImageKHR             eglImage;
    struct wl_buffer       *buffer;
    EGLBoolean              attached;
    struct wl_list          acquiredLink;
} WlEglStreamImage;

typedef struct WlEglSurfaceCtxRec {
    EGLBoolean              isOffscreen;
    EGLSurface              eglSurface;
    EGLStreamKHR            eglStream;
    void                   *wlStreamResource;
    EGLBoolean              isAttached;

    int          useDamageThread;
    pthread_t    damageThreadId;
    EGLSyncKHR   damageThreadSync;
    int          damageThreadFlush;
    int          damageThreadShutdown;
    EGLuint64KHR framesProduced;
    EGLuint64KHR framesFinished;
    EGLuint64KHR framesProcessed;

    /*
     * The double pointer is because of the need to allocate the data for each
     * image slot separately to avoid clobbering the acquiredLink member
     * whenever the streamImages arrary is resized with realloc().
     */
    WlEglStreamImage      **streamImages;
    struct wl_list          acquiredImages;
    struct wl_buffer       *currentBuffer;
    uint32_t                numStreamImages;

    struct wl_list link;
} WlEglSurfaceCtx;

typedef struct WlEglSurfaceRec {
    WlEglDisplay *wlEglDpy;
    EGLConfig     eglConfig;
    EGLint       *attribs;
    EGLBoolean    pendingSwapIntervalUpdate;

    struct wl_egl_window *wlEglWin;
    long int              wlEglWinVer;
    struct wl_surface    *wlSurface;
    int                   width, height;
    int                   dx,    dy;

    WlEglSurfaceCtx ctx;
    struct wl_list  oldCtxList;

    EGLint swapInterval;
    EGLint fifoLength;

    struct wl_callback    *throttleCallback;
    struct wl_event_queue *wlEventQueue;

    /* Asynchronous wl_buffer.release event processing */
    struct {
        struct wl_event_queue  *wlBufferEventQueue;
        pthread_t               bufferReleaseThreadId;
        int                     bufferReleaseThreadPipe[2];
    };

    struct wl_list link;

    EGLBoolean isSurfaceProducer;

    /* The refCount is initialized to 1 during EGLSurface creation,
     * gets incremented/decrementsd in wlEglSurfaceRef()/wlEglSurfaceUnref(),
     * when we enter/exit from eglSwapBuffers().
     */
    unsigned int refCount;
    /*
     * Set to EGL_TRUE before destroying the EGLSurface in eglDestroySurface().
     */
    EGLBoolean isDestroyed;

    /* The lock is used to serialize eglSwapBuffers()/eglDestroySurface(),
     * Using wlExternalApiLock() for this requires that we release lock
     * before dispatching frame sync events in wlEglWaitFrameSync().
     */
    pthread_mutex_t mutexLock;

    /* We want to delay the resizing of the window surface until the next
     * eglSwapBuffers(), so just set a resize flag.
     */
    EGLBoolean isResized;
} WlEglSurface;

WL_EXPORT
EGLBoolean wlEglInitializeSurfaceExport(WlEglSurface *surface);

void wlEglResizeSurfaceIfRequired(WlEglDisplay *display,
                                  WlEglPlatformData *pData,
                                  WlEglSurface *surface);

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
EGLBoolean wlEglIsWlEglSurfaceForDisplay(WlEglDisplay *display, WlEglSurface *wlEglSurface);

EGLBoolean wlEglQueryNativeResourceHook(EGLDisplay dpy,
                                        void *nativeResource,
                                        EGLint attribute,
                                        int *value);

EGLBoolean wlEglSendDamageEvent(WlEglSurface *surface,
                                struct wl_event_queue *queue);

void wlEglCreateFrameSync(WlEglSurface *surface);
EGLint wlEglWaitFrameSync(WlEglSurface *surface);

EGLBoolean wlEglSurfaceRef(WlEglDisplay *display, WlEglSurface *surface);
void wlEglSurfaceUnref(WlEglSurface *surface);

EGLint wlEglHandleImageStreamEvents(WlEglSurface *surface);

#ifdef __cplusplus
}
#endif

#endif
