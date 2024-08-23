/*
 * Copyright (c) 2014-2024, NVIDIA CORPORATION. All rights reserved.
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

#ifndef WAYLAND_EGLSURFACE_INTERNAL_H
#define WAYLAND_EGLSURFACE_INTERNAL_H

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <pthread.h>
#include <wayland-client.h>
#include "wayland-egldisplay.h"
#include "wayland-eglutils.h"
#include "wayland-eglsurface.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct WlEglStreamImageRec {
    /* Pointer back to the parent surface for use in Wayland callbacks */
    struct WlEglSurfaceRec *surface;

    EGLImageKHR             eglImage;
    struct wl_buffer       *buffer;
    EGLBoolean              attached;
    struct wl_list          acquiredLink;

    struct wp_linux_drm_syncobj_timeline_v1 *wlReleaseTimeline;
    uint32_t                drmSyncobjHandle;
    int                     releasePending;
    /* Latest release point the compositor will signal with explicit sync */
    uint64_t                releasePoint;
    /* Cached acquire EGLSync from acquireImage */
    EGLSyncKHR              acquireSync;

    /*
     * Used for delaying the destruction of the image if we are waiting the
     * buffer release thread to use it later.
     */
    EGLBoolean              destructionPending;

    struct wl_list          link;
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
     * Use an individual mutex to guard access to streamImages. This helps us
     * to avoid sharing the surface lock between the app and buffer release
     * event threads, resulting in simplified lock management and smaller
     * critical sections.
     */
    pthread_mutex_t         streamImagesMutex;

    struct wl_list          streamImages;
    struct wl_list          acquiredImages;
    struct wl_buffer       *currentBuffer;

    struct wl_list link;
} WlEglSurfaceCtx;

struct WlEglSurfaceRec {
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

    int (*present_update_callback)(void*, uint64_t, int);
    struct wl_event_queue *presentFeedbackQueue;
    int                   inFlightPresentFeedbackCount;
    int                   landedPresentFeedbackCount;

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

    /* True when the EGL_PRESENT_OPAQUE_EXT surface attrib is set by the app */
    EGLBoolean presentOpaque;

    /* This pair of mutex and conditional variable is used
     * for sychronization between eglSwapBuffers() and damage
     * thread on creating frame sync and waiting for it.
     */
    pthread_mutex_t mutexFrameSync;
    pthread_cond_t condFrameSync;

    /* We want to delay the resizing of the window surface until the next
     * eglSwapBuffers(), so just set a resize flag.
     */
    EGLBoolean isResized;

    WlEglDmaBufFeedback feedback;

    /* per-surface Explicit Sync objects */
    struct wp_linux_drm_syncobj_surface_v1 *wlSyncobjSurf;
    struct wp_linux_drm_syncobj_timeline_v1 *wlAcquireTimeline;
    uint32_t drmSyncobjHandle;
    /* Last acquire point used. This starts at 1, zero means invalid.  */
    uint64_t syncPoint;
};

void wlEglReallocSurface(WlEglDisplay *display,
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

EGLBoolean wlEglQuerySurfaceHook(EGLDisplay dpy, EGLSurface eglSurface, EGLint attribute, EGLint *value);

EGLBoolean wlEglQueryNativeResourceHook(EGLDisplay dpy,
                                        void *nativeResource,
                                        EGLint attribute,
                                        int *value);

EGLBoolean
wlEglSurfaceCheckReleasePoints(WlEglDisplay *display, WlEglSurface *surface);

EGLBoolean wlEglSendDamageEvent(WlEglSurface *surface,
                                struct wl_event_queue *queue,
                                EGLint *rects,
                                EGLint n_rects);

void wlEglCreateFrameSync(WlEglSurface *surface);
EGLint wlEglWaitFrameSync(WlEglSurface *surface);

EGLBoolean wlEglSurfaceRef(WlEglDisplay *display, WlEglSurface *surface);
void wlEglSurfaceUnref(WlEglSurface *surface);

EGLint wlEglHandleImageStreamEvents(WlEglSurface *surface);

#ifdef __cplusplus
}
#endif

#endif
