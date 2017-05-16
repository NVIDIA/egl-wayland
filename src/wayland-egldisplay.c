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

#include "wayland-egldisplay.h"
#include "wayland-eglstream-client-protocol.h"
#include "wayland-eglstream-controller-client-protocol.h"
#include "wayland-eglstream-server.h"
#include "wayland-api-lock.h"
#include "wayland-eglsurface.h"
#include "wayland-eglhandle.h"
#include "wayland-eglutils.h"
#include <string.h>
#include <stdlib.h>

/* TODO: Make global display lists hang off platform data */
static struct wl_list wlEglDisplayList   = WL_LIST_INIT(&wlEglDisplayList);
static struct wl_list wlEglDeviceDpyList = WL_LIST_INIT(&wlEglDeviceDpyList);

EGLBoolean wlEglIsWaylandDisplay(void *nativeDpy)
{
    EGLBoolean ret;

    if (!wlEglPointerIsDereferencable(nativeDpy)) {
        return EGL_FALSE;
    }

    void *first_pointer = *(void **) nativeDpy;

    /* wl_display is a wl_proxy, which is a wl_object.
     * wl_object's first element points to the interfacetype.
     */
    ret = (first_pointer == &wl_display_interface) ? EGL_TRUE : EGL_FALSE;

    return ret;
}

EGLBoolean wlEglIsValidNativeDisplayExport(void *data, void *nativeDpy)
{
    char *val = getenv("EGL_PLATFORM");

    if (val && !strcasecmp(val, "wayland")) {
        return EGL_TRUE;
    }

    return wlEglIsWaylandDisplay(nativeDpy);
}

EGLBoolean wlEglBindDisplaysHook(void *data, EGLDisplay dpy, void *nativeDpy)
{
    EGLBoolean res = EGL_FALSE;

    wlExternalApiLock();

    res = wl_eglstream_display_bind((WlEglPlatformData *)data,
                                    (struct wl_display *)nativeDpy,
                                    dpy) ? EGL_TRUE : EGL_FALSE;

    wlExternalApiUnlock();

    return res;
}

EGLBoolean wlEglUnbindDisplaysHook(EGLDisplay dpy, void *nativeDpy)
{
    struct wl_eglstream_display *wlStreamDpy;
    EGLBoolean res = EGL_FALSE;

    wlExternalApiLock();

    wlStreamDpy = wl_eglstream_display_get(dpy);
    if (wlStreamDpy &&
        (wlStreamDpy->wlDisplay == (struct wl_display *)nativeDpy)) {
        wl_eglstream_display_unbind(wlStreamDpy);
        res = EGL_TRUE;
    }

    wlExternalApiUnlock();

    return res;
}

static void
registry_handle_global(void *data,
                       struct wl_registry *registry,
                       uint32_t name,
                       const char *interface,
                       uint32_t version)
{
    WlEglDisplay *display = (WlEglDisplay *)data;

    if (strcmp(interface, "wl_eglstream_display") == 0) {
        display->wlStreamDpy = wl_registry_bind(registry,
                                                name,
                                                &wl_eglstream_display_interface,
                                                version);
    }
    if (strcmp(interface, "wl_eglstream_controller") == 0) {
        display->wlStreamCtl = wl_registry_bind(registry,
                                                name,
                                                &wl_eglstream_controller_interface,
                                                version);
    }
}

static void
registry_handle_global_remove(void *data,
                              struct wl_registry *registry,
                              uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = {
    registry_handle_global,
    registry_handle_global_remove
};

static void
eglstream_display_handle_caps(void *data,
                              struct wl_eglstream_display *wlStreamDpy,
                              int32_t caps)
{
    WlEglDisplay *dpy = (WlEglDisplay *)data;

#define IS_CAP_SET(CAPS, CAP) (((CAPS)&(CAP)) != 0)

    dpy->caps.stream_fd     = IS_CAP_SET(caps,
                                         WL_EGLSTREAM_DISPLAY_CAP_STREAM_FD);
    dpy->caps.stream_inet   = IS_CAP_SET(caps,
                                         WL_EGLSTREAM_DISPLAY_CAP_STREAM_INET);
    dpy->caps.stream_socket = IS_CAP_SET(caps,
                                         WL_EGLSTREAM_DISPLAY_CAP_STREAM_SOCKET);

#undef IS_CAP_SET
}

static void
eglstream_display_handle_swapinterval_override(
                                    void *data,
                                    struct wl_eglstream_display *wlStreamDpy,
                                    int32_t swapinterval,
                                    struct wl_buffer *streamResource)
{
    WlEglSurface      *surf  = NULL;
    WlEglPlatformData *pData = NULL;
    EGLDisplay         dpy   = EGL_NO_DISPLAY;

    wl_list_for_each(surf, &wlEglSurfaceList, link) {
        if (surf->ctx.wlStreamResource == streamResource) {
           break;
        }
    }

    if (surf == NULL) {
        return;
    }

    pData = surf->wlEglDpy->data;
    dpy   = surf->wlEglDpy->devDpy->eglDisplay;

    if (pData->egl.swapInterval(dpy, swapinterval)) {
        surf->swapInterval = swapinterval;
    }
}


static const struct wl_eglstream_display_listener eglstream_display_listener = {
    eglstream_display_handle_caps,
    eglstream_display_handle_swapinterval_override,
};

static void
sync_callback(void *data, struct wl_callback *callback, uint32_t serial)
{
    int *done = data;

    *done = 1;
    wl_callback_destroy(callback);
}

static const struct wl_callback_listener sync_listener = {
    sync_callback
};

int wlEglRoundtrip(WlEglDisplay *display, struct wl_event_queue *queue)
{
    struct wl_callback *callback;
    int ret = 0, done = 0;

    callback = wl_display_sync(display->nativeDpy);
    wl_callback_add_listener(callback, &sync_listener, &done);
    wl_proxy_set_queue((struct wl_proxy *) callback, queue);

    while (ret != -1 && !done) {
        /* We are handing execution control over to Wayland here, so we need to
         * release the lock just in case it re-enters the external platform (e.g
         * calling into EGL or any of the configured wayland callbacks)
         *
         * XXX: Note that we are using display->wlQueue. If another
         *      thread destroys display while we are still dispatching
         *      events, it will become invalid. We need finer-grained locks to
         *      solve this issue.
         */
        wlExternalApiUnlock();
        ret = wl_display_dispatch_queue(display->nativeDpy,
                                        queue);
        wlExternalApiLock();
    }

    if (!done) {
        wl_callback_destroy(callback);
    }

    return ret;
}

static EGLBoolean terminateDisplay(EGLDisplay dpy)
{
    WlEglDisplay      *display       = (WlEglDisplay *)dpy;
    WlEglPlatformData *data          = display->data;
    EGLBoolean         fullTerminate = EGL_FALSE;
    EGLBoolean         res           = EGL_TRUE;

    /* First, destroy any surface associated to the given display. Then
     * destroy the display connection itself */
    wlEglDestroyAllSurfaces(display);

    if (display->wlRegistry) {
        wl_registry_destroy(display->wlRegistry);
    }
    if (display->wlQueue) {
        wl_event_queue_destroy(display->wlQueue);
    }
    if (display->wlDamageEventQueue) {
        wl_event_queue_destroy(display->wlDamageEventQueue);
    }
    if (display->wlStreamDpy) {
        wl_eglstream_display_destroy(display->wlStreamDpy);
    }
    if (display->ownNativeDpy) {
        wl_display_disconnect(display->nativeDpy);
    }

    /* Unreference internal EGL display. If refCount reaches 0, mark the
     * internal display for termination. */
    if (display->devDpy != NULL) {
        display->devDpy->refCount--;
        if (display->devDpy->refCount == 0) {
            /* Save the internal EGLDisplay handle, as it's needed by the actual
             * eglTerminate() call */
            fullTerminate = EGL_TRUE;
            dpy           = display->devDpy->eglDisplay;

            wl_list_remove(&display->devDpy->link);
            free(display->devDpy);
        }
    }

    /* Destroy the external display */
    wl_list_remove(&display->link);
    free(display);

    /* XXX: Currently, we assume an internal EGLDisplay will only be used by a
     *      single external platform. In practice, we could create multiple
     *      external displays from different external implementations using the
     *      same internal EGLDisplay. This is a known issue currently tracked in
     *      the following Khronos bug:
     *
     *       https://cvs.khronos.org/bugzilla/show_bug.cgi?id=12072
     */
    if (fullTerminate) {
        wlExternalApiUnlock();
        res = data->egl.terminate(dpy);
        wlExternalApiLock();
    }

    return res;
}

EGLBoolean wlEglTerminateHook(EGLDisplay dpy)
{
    EGLBoolean res;

    wlExternalApiLock();
    res = terminateDisplay(dpy);
    wlExternalApiUnlock();

    return res;
}

EGLDisplay wlEglGetPlatformDisplayExport(void *data,
                                         EGLenum platform,
                                         void *nativeDpy,
                                         const EGLAttrib *attribs)
{
    WlEglPlatformData *pData        = (WlEglPlatformData *)data;
    WlEglDeviceDpy    *devDpy       = NULL;
    WlEglDisplay      *display      = NULL;
    EGLBoolean         ownNativeDpy = EGL_FALSE;
    EGLint             numDevices   = 0;
    int                ret          = 0;

    if (platform != EGL_PLATFORM_WAYLAND_EXT) {
        wlEglSetError(data, EGL_BAD_PARAMETER);
        return EGL_NO_DISPLAY;
    }

    wlExternalApiLock();

    /* If default display is requested, create a new Wayland display connection
     * and its corresponding internal EGLDisplay. Otherwise, check for existing
     * associated EGLDisplay for the given Wayland display and if it doesn't
     * exist, create a new one */
    if (!nativeDpy) {
        nativeDpy = wl_display_connect(NULL);
        if (!nativeDpy) {
            wlExternalApiUnlock();
            return EGL_NO_DISPLAY;
        }

        ownNativeDpy = EGL_TRUE;
        wl_display_dispatch_pending(nativeDpy);
    } else {
        wl_list_for_each(display, &wlEglDisplayList, link) {
            if (display->nativeDpy == nativeDpy) {
                wlExternalApiUnlock();
                return (EGLDisplay)display;
            }
        }
    }

    display = calloc(1, sizeof(*display));
    if (!display) {
        wlExternalApiUnlock();
        goto fail;
    }

    wl_list_insert(&wlEglDisplayList, &display->link);

    display->data = pData;

    display->ownNativeDpy = ownNativeDpy;
    display->nativeDpy    = nativeDpy;

    /* Listen to wl_registry events and make a roundtrip in order to find the
     * wl_eglstream_display global object */
    display->wlQueue = wl_display_create_queue(nativeDpy);
    display->wlDamageEventQueue = wl_display_create_queue(nativeDpy);
    display->wlRegistry = wl_display_get_registry(nativeDpy);
    wl_proxy_set_queue((struct wl_proxy *)display->wlRegistry,
                        display->wlQueue);
    wl_registry_add_listener(display->wlRegistry, &registry_listener, display);

    ret = wlEglRoundtrip(display, display->wlQueue);
    if (ret < 0 || !display->wlStreamDpy) {
        goto fail;
    }

    /* Listen to wl_eglstream_display events and make another roundtrip so we
     * catch any bind-related event (e.g. server capabilities) */
    wl_proxy_set_queue((struct wl_proxy *)display->wlStreamDpy,
                       display->wlQueue);
    wl_eglstream_display_add_listener(display->wlStreamDpy,
                                      &eglstream_display_listener,
                                      display);
    ret = wlEglRoundtrip(display, display->wlQueue);
    if (ret < 0) {
        goto fail;
    }

    /* Restore global registry default queue as other clients may rely on it and
       we are no longer interested in registry events */
    wl_proxy_set_queue((struct wl_proxy *)display->wlRegistry, NULL);

    /* Get number of available EGL devices */
    if (!pData->egl.queryDevices(0, NULL, &numDevices) || numDevices == 0) {
        goto fail;
    }

    /*
     * Seek for the desired device in wlEglDeviceDpyList. If found, use it;
     * otherwise, create a new WlEglDeviceDpy and add it to wlEglDeviceDpyList
     */
    wl_list_for_each(devDpy, &wlEglDeviceDpyList, link) {
        /* TODO: Add support for multiple devices/device selection */
        display->devDpy = devDpy;
        break;
    }

    if (!display->devDpy) {
        devDpy = calloc(1, sizeof(*devDpy));
        if (!devDpy) {
            goto fail;
        }

        if (!pData->egl.queryDevices(1, &devDpy->eglDevice, &numDevices)) {
            goto fail;
        }

        /* TODO: Parse <attribs> and filter out incompatible attribues */
        devDpy->eglDisplay =
            pData->egl.getPlatformDisplay(EGL_PLATFORM_DEVICE_EXT,
                                          devDpy->eglDevice,
                                          NULL);
        if (devDpy->eglDisplay == EGL_NO_DISPLAY) {
            goto fail;
        }

        wl_list_insert(&wlEglDeviceDpyList, &devDpy->link);

        display->devDpy = devDpy;
    }

    display->devDpy->refCount++;

    wlExternalApiUnlock();
    return display;

fail:
    wlExternalApiUnlock();

    if (display) {
        wlEglTerminateHook(display);
    } else if (ownNativeDpy) {
        wl_display_disconnect(nativeDpy);
    }

    free(devDpy);

    return EGL_NO_DISPLAY;
}

EGLBoolean wlEglInitializeHook(EGLDisplay dpy, EGLint *major, EGLint *minor)
{
    WlEglDisplay      *display = (WlEglDisplay *)dpy;
    WlEglPlatformData *data    = display->data;
    EGLBoolean         res     = EGL_FALSE;

    dpy = display->devDpy->eglDisplay;
    res = data->egl.initialize(dpy, major, minor);

    if (res) {
        const char *exts = data->egl.queryString(dpy, EGL_EXTENSIONS);

        wlExternalApiLock();

#define CACHE_EXT(_PREFIX_, _NAME_)                                      \
        display->exts._NAME_ =                                           \
            !!wlEglFindExtension("EGL_" #_PREFIX_ "_" #_NAME_, exts)

        CACHE_EXT(KHR, stream);
        CACHE_EXT(NV,  stream_attrib);
        CACHE_EXT(KHR, stream_cross_process_fd);
        CACHE_EXT(NV,  stream_remote);
        CACHE_EXT(KHR, stream_producer_eglsurface);
        CACHE_EXT(NV,  stream_fifo_synchronous);
        CACHE_EXT(NV,  stream_sync);

#undef CACHE_EXT

        wlExternalApiUnlock();
    }

    return res;
}

EGLBoolean wlEglIsWlEglDisplay(WlEglDisplay *display)
{
    WlEglDisplay *dpy;

    wl_list_for_each(dpy, &wlEglDisplayList, link) {
        if (dpy == display) {
            return EGL_TRUE;
        }
    }

    return EGL_FALSE;
}

EGLBoolean wlEglChooseConfigHook(EGLDisplay dpy,
                                 EGLint const *attribs,
                                 EGLConfig *configs,
                                 EGLint configSize,
                                 EGLint *numConfig)
{
    WlEglDisplay      *display  = (WlEglDisplay *)dpy;
    WlEglPlatformData *data     = display->data;
    EGLint            *attribs2 = NULL;
    EGLint             nAttribs = 0;
    EGLBoolean         surfType = EGL_FALSE;
    EGLint             err      = EGL_SUCCESS;
    EGLBoolean         ret;

    /* Save the internal EGLDisplay handle, as it's needed by the actual
     * eglChooseConfig() call */
    dpy = display->devDpy->eglDisplay;

    /* Calculate number of attributes in attribs */
    if (attribs) {
        while (attribs[nAttribs] != EGL_NONE) {
            surfType = surfType || (attribs[nAttribs] == EGL_SURFACE_TYPE);
            nAttribs += 2;
        }
    }

    /* If not SURFACE_TYPE provided, we need convert the default WINDOW_BIT to a
     * default EGL_STREAM_BIT */
    nAttribs += (surfType ? 0 : 2);

    /* Make attributes list copy */
    attribs2 = (EGLint *)malloc((nAttribs + 1) * sizeof(*attribs2));
    if (!attribs2) {
        err = EGL_BAD_ALLOC;
        goto done;
    }

    memcpy(attribs2, attribs, nAttribs * sizeof(*attribs2));
    attribs2[nAttribs] = EGL_NONE;

    /* Replace all WINDOW_BITs by EGL_STREAM_BITs */
    if (surfType) {
        nAttribs = 0;
        while (attribs2[nAttribs] != EGL_NONE) {
            if ((attribs2[nAttribs] == EGL_SURFACE_TYPE) &&
                (attribs2[nAttribs + 1] != EGL_DONT_CARE) &&
                (attribs2[nAttribs + 1] & EGL_WINDOW_BIT)) {
                attribs2[nAttribs + 1] &= ~EGL_WINDOW_BIT;
                attribs2[nAttribs + 1] |= EGL_STREAM_BIT_KHR;
            }
            nAttribs += 2;
        }
    } else {
        attribs2[nAttribs - 2] = EGL_SURFACE_TYPE;
        attribs2[nAttribs - 1] = EGL_STREAM_BIT_KHR;
    }

    /* Actual eglChooseConfig() call */
    ret = data->egl.chooseConfig(dpy,
                                 attribs2,
                                 configs,
                                 configSize,
                                 numConfig);

done:
    /* Cleanup */
    free(attribs2);

    if (err != EGL_SUCCESS) {
        wlEglSetError(data, err);
        return EGL_FALSE;
    }

    return ret;
}

EGLBoolean wlEglGetConfigAttribHook(EGLDisplay dpy,
                                    EGLConfig config,
                                    EGLint attribute,
                                    EGLint *value)
{
    WlEglDisplay      *display = (WlEglDisplay *)dpy;
    WlEglPlatformData *data    = display->data;
    EGLBoolean         ret     = EGL_FALSE;

    /* Save the internal EGLDisplay handle, as it's needed by the actual
     * eglGetConfigAttrib() call */
    dpy = display->devDpy->eglDisplay;

    ret = data->egl.getConfigAttrib(dpy, config, attribute, value);
    if (ret && (attribute == EGL_SURFACE_TYPE)) {
        /* We only support window configurations through EGLStreams */
        if (*value & EGL_STREAM_BIT_KHR) {
            *value |= EGL_WINDOW_BIT;
        } else {
            *value &= ~EGL_WINDOW_BIT;
        }
    }

    return ret;
}

EGLBoolean wlEglDestroyAllDisplays(WlEglPlatformData *data)
{
    WlEglDisplay *display, *next;
    EGLBoolean res = EGL_TRUE;

    wlExternalApiLock();

    wl_list_for_each_safe(display, next, &wlEglDisplayList, link) {
        if (display->data == data) {
            res = terminateDisplay((EGLDisplay)display) && res;
        }
    }

    wlExternalApiUnlock();

    return res;
}

const char* wlEglQueryStringExport(void *data,
                                   EGLDisplay dpy,
                                   EGLExtPlatformString name)
{
    WlEglPlatformData *pData   = (WlEglPlatformData *)data;
    WlEglDisplay      *display = (WlEglDisplay *)dpy;
    const char        *res     = NULL;

    switch (name) {
    case EGL_EXT_PLATFORM_PLATFORM_CLIENT_EXTENSIONS:
        res = "EGL_EXT_platform_wayland";
        break;

    case EGL_EXT_PLATFORM_DISPLAY_EXTENSIONS:
        if (dpy == EGL_NO_DISPLAY) {
            /* This should return all client extensions, which for now is
             * equivalent to EXTERNAL_PLATFORM_CLIENT_EXTENSIONS */
            res = "EGL_EXT_platform_wayland";
        } else {
            EGLBoolean isWlEglDpy = EGL_FALSE;

            wlExternalApiLock();
            isWlEglDpy = wlEglIsWlEglDisplay(display);
            wlExternalApiUnlock();

            if (isWlEglDpy) {
                res = "EGL_WL_bind_wayland_display "
                      "EGL_WL_wayland_eglstream";
            } else {
                /*
                 * Check whether the given display supports EGLStream
                 * extensions. For Wayland support over EGLStreams, at least the
                 * following extensions must be supported by the underlying
                 * driver:
                 *
                 *  - EGL_KHR_stream
                 *  - EGL_KHR_stream_producer_eglsurface
                 *  - EGL_KHR_stream_cross_process_fd
                 */
                const char *exts = pData->egl.queryString(dpy, EGL_EXTENSIONS);

                if (wlEglFindExtension("EGL_KHR_stream", exts) &&
                    wlEglFindExtension("EGL_KHR_stream_producer_eglsurface", exts) &&
                    wlEglFindExtension("EGL_KHR_stream_cross_process_fd", exts)) {
                    res = "EGL_WL_bind_wayland_display "
                          "EGL_WL_wayland_eglstream";
                }
            }
        }
        break;

    default:
        break;
    }

    return res;
}
