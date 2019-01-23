/*
 * Copyright (c) 2014-2018, NVIDIA CORPORATION. All rights reserved.
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
#include <wayland-egl-backend.h>

EGLBoolean wlEglSwapBuffersHook(EGLDisplay eglDisplay, EGLSurface eglSurface)
{
    return wlEglSwapBuffersWithDamageHook(eglDisplay, eglSurface, NULL, 0);
}

EGLBoolean wlEglSwapBuffersWithDamageHook(EGLDisplay eglDisplay, EGLSurface eglSurface, EGLint *rects, EGLint n_rects)
{
    WlEglDisplay          *display     = (WlEglDisplay *)eglDisplay;
    WlEglPlatformData     *data        = display->data;
    WlEglSurface          *surface     = (WlEglSurface *)eglSurface;
    struct wl_event_queue *queue       = NULL;
    EGLStreamKHR           eglStream   = EGL_NO_STREAM_KHR;
    EGLBoolean             isOffscreen = EGL_FALSE;
    EGLBoolean             res;
    EGLint                 err;

    wlExternalApiLock();

    if (!wlEglIsWaylandDisplay(display->nativeDpy)) {
        err = EGL_BAD_DISPLAY;
        goto fail;
    }

    if (!wlEglIsWlEglSurface(surface)) {
        err = EGL_BAD_SURFACE;
        goto fail;
    }

    isOffscreen = surface->ctx.isOffscreen;

    if (!isOffscreen) {
        queue = wlGetEventQueue(display);
        if (queue == NULL) {
            err = EGL_CONTEXT_LOST; /* XXX: Is this the right error? */
            goto fail;
        }

        if (!wlEglIsWaylandWindowValid(surface->wlEglWin)) {
            err = EGL_BAD_SURFACE;
            goto fail;
        }

        /* Bail out if we were left with an invalid surface */
        if (wlEglWaitFrameSync(surface, queue) == EGL_BAD_SURFACE) {
            err = EGL_BAD_SURFACE;
            goto fail;
        }
    }

    /* Save the internal EGLDisplay, EGLSurface and EGLStream handles, as
     * they are needed by the eglSwapBuffers() and streamFlush calls below */
    eglDisplay = display->devDpy->eglDisplay;
    eglSurface = surface->ctx.eglSurface;
    eglStream = surface->ctx.eglStream;

    /* eglSwapBuffers() is a blocking call. We must release the lock so other
     * threads using the external platform are allowed to progress.
     */
    wlExternalApiUnlock();
    if (rects) {
        res = data->egl.swapBuffersWithDamage(eglDisplay, eglSurface, rects, n_rects);
    } else {
        res = data->egl.swapBuffers(eglDisplay, eglSurface);
    }
    if (isOffscreen) {
        goto done;
    }
    if (display->exts.stream_flush) {
        data->egl.streamFlush(eglDisplay, eglStream);
    }
    wlExternalApiLock();

    /* Bail out if the surface was destroyed while the lock was suspended */
    if (!wlEglIsWlEglSurface(surface)) {
        err = EGL_BAD_SURFACE;
        goto fail;
    }

    if (res) {
        if (surface->ctx.useDamageThread) {
            surface->ctx.framesProduced++;
        } else {
            res = wlEglSendDamageEvent(surface, queue);
        }
    }
    wlEglCreateFrameSync(surface, queue);
    wlExternalApiUnlock();

done:
    return res;

fail:
    wlExternalApiUnlock();
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

    wl_eglstream_display_swap_interval(display->wlStreamDpy,
                                       surface->ctx.wlStreamResource,
                                       interval);

    /* Cache interval value so we can reset it upon surface reattach */
    surface->swapInterval = interval;

done:
    wlExternalApiUnlock();

    return EGL_TRUE;
}

EGLBoolean wlEglPrePresentExport(WlEglSurface *surface) {
    WlEglDisplay          *display = surface->wlEglDpy;
    struct wl_event_queue *queue   = NULL;

    wlExternalApiLock();

    queue = wlGetEventQueue(display);
    if (queue == NULL) {
        return EGL_FALSE;
    }

    wlEglWaitFrameSync(surface, queue);

    wlExternalApiUnlock();

    return EGL_TRUE;
}

EGLBoolean wlEglPostPresentExport(WlEglSurface *surface) {
    WlEglDisplay          *display = surface->wlEglDpy;
    WlEglPlatformData     *data    = display->data;
    struct wl_event_queue *queue   = NULL;
    EGLBoolean             res     = EGL_TRUE;

    if (display->exts.stream_flush) {
        data->egl.streamFlush((EGLDisplay) display, surface->ctx.eglStream);
    }

    wlExternalApiLock();

    queue = wlGetEventQueue(display);
    if (queue == NULL) {
        return EGL_FALSE;
    }

    if (surface->ctx.useDamageThread) {
        surface->ctx.framesProduced++;
    } else {
        res = wlEglSendDamageEvent(surface, queue);
    }

    wlEglCreateFrameSync(surface, queue);

    wlExternalApiUnlock();

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
