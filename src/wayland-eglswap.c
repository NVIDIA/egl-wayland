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

#include "wayland-eglswap.h"
#include "wayland-eglstream-client-protocol.h"
#include "wayland-thread.h"
#include "wayland-egldisplay.h"
#include "wayland-eglsurface.h"
#include "wayland-eglhandle.h"
#include "wayland-eglutils.h"
#include <assert.h>
#include <wayland-egl-backend.h>

EGLBoolean wlEglSwapBuffersHook(EGLDisplay eglDisplay, EGLSurface eglSurface)
{
    return wlEglSwapBuffersWithDamageHook(eglDisplay, eglSurface, NULL, 0);
}

EGLBoolean wlEglSwapBuffersWithDamageHook(EGLDisplay eglDisplay, EGLSurface eglSurface, EGLint *rects, EGLint n_rects)
{
    WlEglDisplay          *display     = (WlEglDisplay *)eglDisplay;
    WlEglPlatformData     *data        = display->data;
    WlEglSurface          *surface     = NULL;
    EGLStreamKHR           eglStream   = EGL_NO_STREAM_KHR;
    EGLBoolean             isOffscreen = EGL_FALSE;
    EGLBoolean             res;
    EGLint                 err;

    if (display->initCount == 0) {
        err = EGL_NOT_INITIALIZED;
        goto fail;
    }

    //wlEglSurfaceRef() requires we acquire wlExternalApiLock.
    wlExternalApiLock();
    if (!wlEglSurfaceRef(eglSurface)) {
        wlExternalApiUnlock();
        err = EGL_BAD_SURFACE;
        goto fail;
    }

    surface = eglSurface;
    if (surface->pendingSwapIntervalUpdate == EGL_TRUE) {
        /* Send request from client to override swapinterval value based on
         * server's swapinterval for overlay compositing
         */
        wl_eglstream_display_swap_interval(display->wlStreamDpy,
                                           surface->ctx.wlStreamResource,
                                           surface->swapInterval);
        /* For receiving any event in case of override */
        if (wl_display_roundtrip_queue(display->nativeDpy,
                                       display->wlEventQueue) < 0) {
            err = EGL_BAD_ALLOC;
            wlExternalApiUnlock();
            goto fail;
        }
        surface->pendingSwapIntervalUpdate = EGL_FALSE;
    }

    wlExternalApiUnlock();


    // Acquire wlEglSurface lock.
    pthread_mutex_lock(&surface->mutexLock);

    if (surface->isDestroyed) {
        err = EGL_BAD_SURFACE;
        goto fail_locked;
    }

    isOffscreen = surface->ctx.isOffscreen;
    if (!isOffscreen) {
        if (!wlEglIsWaylandWindowValid(surface->wlEglWin)) {
            err = EGL_BAD_NATIVE_WINDOW;
            goto fail_locked;
        }

        wlEglWaitFrameSync(surface);
    }

    /* Save the internal EGLDisplay, EGLSurface and EGLStream handles, as
     * they are needed by the eglSwapBuffers() and streamFlush calls below */
    eglDisplay = display->devDpy->eglDisplay;
    eglSurface = surface->ctx.eglSurface;
    eglStream = surface->ctx.eglStream;

    /* eglSwapBuffers() is a blocking call. We must release the lock so other
     * threads using the external platform are allowed to progress.
     */
    if (rects) {
        res = data->egl.swapBuffersWithDamage(eglDisplay, eglSurface, rects, n_rects);
    } else {
        res = data->egl.swapBuffers(eglDisplay, eglSurface);
    }
    if (isOffscreen) {
        goto done;
    }
    if (display->devDpy->exts.stream_flush) {
        data->egl.streamFlush(eglDisplay, eglStream);
    }

    if (res) {
        if (surface->ctx.useDamageThread) {
            surface->ctx.framesProduced++;
        } else {
            res = wlEglSendDamageEvent(surface, surface->wlEventQueue);
        }
    }
    wlEglCreateFrameSync(surface);

done:
    // Release wlEglSurface lock.
    pthread_mutex_unlock(&surface->mutexLock);

    //wlEglSurfaceUnRef() requires we acquire wlExternalApiLock.
    wlExternalApiLock();
    wlEglSurfaceUnref(surface);
    wlExternalApiUnlock();
    return res;

fail_locked:
    pthread_mutex_unlock(&surface->mutexLock);
fail:
    if (surface != NULL) {
        //wlEglSurfaceUnRef() requires we acquire wlExternalApiLock.
        wlExternalApiLock();
        wlEglSurfaceUnref(surface);
        wlExternalApiUnlock();
    }
    wlEglSetError(data, err);
    return EGL_FALSE;
}

EGLBoolean wlEglSwapIntervalHook(EGLDisplay eglDisplay, EGLint interval)
{
    WlEglDisplay      *display = (WlEglDisplay *)eglDisplay;
    WlEglPlatformData *data    = display->data;
    WlEglSurface      *surface = NULL;
    EGLint             state;

    /* Save the internal EGLDisplay handle, as it's needed by the actual
     * eglSwapInterval() call */
    eglDisplay = display->devDpy->eglDisplay;

    if (!(data->egl.swapInterval(eglDisplay, interval))) {
        return EGL_FALSE;
    }

    surface = (WlEglSurface *)data->egl.getCurrentSurface(EGL_DRAW);

    wlExternalApiLock();

    /* Check this is a valid wayland EGL surface (and stream) before sending the
     * swap interval value to the consumer */
    if (!wlEglIsWlEglSurface(surface) ||
        (surface->swapInterval == interval) ||
        (surface->ctx.wlStreamResource == NULL) ||
        (surface->ctx.eglStream == EGL_NO_STREAM_KHR) ||
        (data->egl.queryStream(display->devDpy->eglDisplay,
                               surface->ctx.eglStream,
                               EGL_STREAM_STATE_KHR,
                               &state) == EGL_FALSE) ||
        (state == EGL_STREAM_STATE_DISCONNECTED_KHR))
    {
        goto done;
    }

    /* Cache interval value so we can reset it upon surface reattach */
    surface->swapInterval = interval;

    /* Set client's pendingSwapIntervalUpdate for updating client's
     * swapinterval
     */
    surface->pendingSwapIntervalUpdate = EGL_TRUE;

done:
    wlExternalApiUnlock();

    return EGL_TRUE;
}

EGLBoolean wlEglPrePresentExport(WlEglSurface *surface) {

    wlExternalApiLock();
    if (surface->pendingSwapIntervalUpdate == EGL_TRUE) {
        /* Send request from client to override swapinterval value based on
         * server's swapinterval for overlay compositing
         */
        wl_eglstream_display_swap_interval(surface->wlEglDpy->wlStreamDpy,
                                           surface->ctx.wlStreamResource,
                                           surface->swapInterval);
        /* For receiving any event in case of override */
        if (wl_display_roundtrip_queue(surface->wlEglDpy->nativeDpy,
                                       surface->wlEglDpy->wlEventQueue) < 0) {
            wlExternalApiUnlock();
            return EGL_FALSE;
        }
        surface->pendingSwapIntervalUpdate = EGL_FALSE;
    }

    wlExternalApiUnlock();

    // Acquire wlEglSurface lock.
    pthread_mutex_lock(&surface->mutexLock);

    wlEglWaitFrameSync(surface);

    // Release wlEglSurface lock.
    pthread_mutex_unlock(&surface->mutexLock);

    return EGL_TRUE;
}

EGLBoolean wlEglPostPresentExport(WlEglSurface *surface) {
    WlEglDisplay          *display = surface->wlEglDpy;
    WlEglPlatformData     *data    = display->data;
    EGLBoolean             res     = EGL_TRUE;

    // Acquire wlEglSurface lock.
    pthread_mutex_lock(&surface->mutexLock);

    if (display->devDpy->exts.stream_flush) {
        data->egl.streamFlush((EGLDisplay) display, surface->ctx.eglStream);
    }


    if (surface->ctx.useDamageThread) {
        surface->ctx.framesProduced++;
    } else {
        res = wlEglSendDamageEvent(surface, surface->wlEventQueue);
    }

    wlEglCreateFrameSync(surface);

    // Release wlEglSurface lock.
    pthread_mutex_unlock(&surface->mutexLock);


    return res;
}

EGLint wlEglStreamSwapIntervalCallback(WlEglPlatformData *data,
                                       EGLStreamKHR stream,
                                       EGLint *interval)
{
    EGLint res = EGL_SUCCESS;

    if (data->callbacks.streamSwapInterval) {
        res = data->callbacks.streamSwapInterval(stream, interval);
    }

    return res;
}
