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

#include "wayland-egldisplay.h"
#include "wayland-eglstream-client-protocol.h"
#include "wayland-eglstream-controller-client-protocol.h"
#include "wayland-eglstream-server.h"
#include "wayland-thread.h"
#include "wayland-eglsurface.h"
#include "wayland-eglhandle.h"
#include "wayland-eglutils.h"
#include <string.h>
#include <stdlib.h>
#include <assert.h>

/* TODO: Make global display lists hang off platform data */
static struct wl_list wlEglDisplayList   = WL_LIST_INIT(&wlEglDisplayList);
static struct wl_list wlEglDeviceDpyList = WL_LIST_INIT(&wlEglDeviceDpyList);

EGLBoolean wlEglIsWaylandDisplay(void *nativeDpy)
{
#if HAS_MINCORE
    if (!wlEglPointerIsDereferencable(nativeDpy)) {
        return EGL_FALSE;
    }

    return wlEglCheckInterfaceType((struct wl_object *)nativeDpy,
                                   "wl_display_interface");
#else
    (void)nativeDpy;

    /* we return EGL_TRUE in order to always assume a valid wayland
     * display is given so that we bypass all the checks that would
     * prevent any of the functions in this library to work
     * otherwise.
     */
    return EGL_TRUE;
#endif
}

EGLBoolean wlEglIsValidNativeDisplayExport(void *data, void *nativeDpy)
{
    EGLBoolean  checkDpy = EGL_TRUE;
    char       *val      = getenv("EGL_PLATFORM");
    (void)data;

    if (val && !strcasecmp(val, "wayland")) {
        return EGL_TRUE;
    }

#if !HAS_MINCORE
    /* wlEglIsWaylandDisplay always returns true if  mincore(2)
     * is not available, hence we cannot ascertain whether the
     * the nativeDpy is wayland.
     * Note: this effectively forces applications to use
     * eglGetPlatformDisplay() instead of eglGetDisplay().
     */
    checkDpy = EGL_FALSE;
#endif

    return (checkDpy ? wlEglIsWaylandDisplay(nativeDpy) : EGL_FALSE);
}

EGLBoolean wlEglBindDisplaysHook(void *data, EGLDisplay dpy, void *nativeDpy)
{
    EGLBoolean res = EGL_FALSE;

    wlExternalApiLock();

    res = wl_eglstream_display_bind((WlEglPlatformData *)data,
                                    (struct wl_display *)nativeDpy,
                                    dpy);

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
        display->wlStreamCtlVer = version;
    }
}

static void
registry_handle_global_remove(void *data,
                              struct wl_registry *registry,
                              uint32_t name)
{
    (void) data;
    (void) registry;
    (void) name;
}

static const struct wl_registry_listener registry_listener = {
    registry_handle_global,
    registry_handle_global_remove
};

static void
registry_handle_global_check_eglstream(
                       void *data,
                       struct wl_registry *registry,
                       uint32_t name,
                       const char *interface,
                       uint32_t version)
{
    EGLBoolean *hasEglStream = (EGLBoolean *)data;
    (void) registry;
    (void) name;
    (void) interface;
    (void) version;

    if (strcmp(interface, "wl_eglstream_display") == 0) {
        *hasEglStream = EGL_TRUE;
    }
}

static void
eglstream_display_handle_caps(void *data,
                              struct wl_eglstream_display *wlStreamDpy,
                              int32_t caps)
{
    WlEglDisplay *dpy = (WlEglDisplay *)data;
    (void) wlStreamDpy;

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
    WlEglSurface *surf = NULL;
    (void) data;
    (void) wlStreamDpy;

    wl_list_for_each(surf, &wlEglSurfaceList, link) {
        if (surf->ctx.wlStreamResource == streamResource) {
            WlEglPlatformData *pData = surf->wlEglDpy->data;
            EGLDisplay         dpy   = surf->wlEglDpy->devDpy->eglDisplay;

            if (pData->egl.swapInterval(dpy, swapinterval)) {
                surf->swapInterval = swapinterval;
            }

            break;
        }
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
    (void) serial;

    *done = 1;
    wl_callback_destroy(callback);
}

static const struct wl_callback_listener sync_listener = {
    sync_callback
};

struct wl_event_queue* wlGetEventQueue(WlEglDisplay *display)
{
    WlThread     *wlThread = wlGetThread();
    WlEventQueue *evtQueue = NULL;
    WlEventQueue *iter     = NULL;

    if (wlThread != NULL) {
        /* Try to find an existing queue in the thread list */
        wl_list_for_each(iter, &wlThread->evtQueueList, threadLink) {
            if (iter->display == display) {
                evtQueue = iter;
                break;
            }
        }

        /* If a valid queue was found, we are done */
        if ((evtQueue != NULL) && (evtQueue->queue != NULL)) {
            return evtQueue->queue;
        }

        if (evtQueue == NULL) {
            /* Create the WlEventQueue and add it to the thread list */
            evtQueue = calloc(1, sizeof(*evtQueue));
            if (evtQueue == NULL) {
                return NULL;
            }

            evtQueue->display = display;

            wl_list_insert(&wlThread->evtQueueList, &evtQueue->threadLink);
        }

        /* Create the wl_event_queue, and add WlEventQueue to the dpy list */
        evtQueue->queue = wl_display_create_queue(display->nativeDpy);
        if (evtQueue->queue == NULL) {
            return NULL;
        }

        wl_list_insert(&display->evtQueueList,  &evtQueue->dpyLink);
    }

    return (evtQueue ? evtQueue->queue : NULL);
}

void wlUpdateQueueBusyStatus(WlEglDisplay *display, struct wl_event_queue *queue, EGLBoolean isBusy)
{
    WlEventQueue *evtQueue = NULL;
    WlEventQueue *iter     = NULL;
    WlEventQueue *tmp      = NULL;

    if (display != NULL) {
        assert(queue);

        /* Try to find the queue from dpy queue list */
        wl_list_for_each(iter, &display->evtQueueList, dpyLink) {
            if (iter->queue == queue) {
                evtQueue = iter;
                break;
            }
        }

        if (evtQueue) {
            if (isBusy) {
                evtQueue->refCount++;
            }
            else {
                /* refCount should not be already 0 */
                assert(evtQueue->refCount > 0);
                evtQueue->refCount--;
            }
        }
        /* If it isn't present in evtQueueList, check the
         * availability in dangling queue list if the isBusy flag is FALSE.
         * If it is present in dangling queue list and the refCnt reaches 0,
         * invalidate and destroy */
        else if (!isBusy) {
            wl_list_for_each_safe(iter, tmp, &display->dangEvtQueueList, dangLink) {
                if (iter->queue == queue) {
                    iter->refCount--;
                    if (!iter->refCount) {
                        wl_event_queue_destroy(iter->queue);
                        iter->queue = NULL;
                        wl_list_remove(&iter->dangLink);
                        free(iter);
                    }
                    break;
                }
            }
        }
    }
}

int wlEglRoundtrip(WlEglDisplay *display, struct wl_event_queue *queue, EGLBoolean lockSuspend)
{
    struct wl_display *wrapper;
    struct wl_callback *callback;
    int ret = 0, done = 0;

    wrapper = wl_proxy_create_wrapper(display->nativeDpy);
    wl_proxy_set_queue((struct wl_proxy *)wrapper, queue);
    callback = wl_display_sync(wrapper);
    wl_proxy_wrapper_destroy(wrapper); /* Done with wrapper */
    ret = wl_callback_add_listener(callback, &sync_listener, &done);

    while (ret != -1 && !done) {
        /* We are handing execution control over to Wayland here, so we need to
         * release the lock just in case it re-enters the external platform (e.g
         * calling into EGL or any of the configured wayland callbacks)
         */
        if (lockSuspend) {
            wlExternalApiUnlock();
        }
        ret = wl_display_dispatch_queue(display->nativeDpy, queue);
        if (lockSuspend) {
            wlExternalApiLock();
        }
    }

    if (!done) {
        wl_callback_destroy(callback);
    }

    return ret;
}

/* On wayland, when a wl_display backed EGLDisplay is created and then
 * wl_display is destroyed without terminating EGLDisplay first, some
 * driver allocated resources associated with wl_display could not be
 * destroyed properly during EGL teardown.
 * Per EGL spec: Termination of a display that has already been terminated,
 * or has not yet been initialized, is allowed, but the only effect of such
 * a call is to return EGL_TRUE, since there are no EGL resources associated
 * with the display to release.
 * However, in our wayland egl driver, we do allocate some resources
 * which are associated with wl_display even eglInitialize is not called.
 * If the app does not terminate EGLDisplay before closing wl_display,
 * it can hit assertion or hang in pthread_mutex_lock during EGL teardown.
 * To WAR the issue, in case wl_display has been destroyed, we skip
 * destroying some resources during EGL system termination, only when
 * terminateDisplay is called from wlEglDestroyAllDisplays.
 */
static EGLBoolean terminateDisplay(EGLDisplay dpy, EGLBoolean skipDestroy)
{
    WlEglDisplay      *display       = (WlEglDisplay *)dpy;
    WlEglPlatformData *data          = display->data;
    WlEventQueue      *iter          = NULL;
    WlEventQueue      *tmp           = NULL;
    EGLBoolean         fullTerminate = EGL_FALSE;
    EGLBoolean         res           = EGL_TRUE;

    if (!display->initialized) {
        return EGL_TRUE;
    }

    if (display->useRefCount) {
        display->refCount -= 1;
        if (display->refCount > 0) {
            return EGL_TRUE;
        }
    }

    /* Mark the display as terminated before calling into
     * wlEglDestroyAllSurfaces to avoid multiple threads trying to teminate
     * the same display simultaneously. When wlEglDestroyAllSurfaces was
     * called, wl external API lock could be released temporarily, which
     * would allow multiple threads get the same display, enter
     * terminateDisplay and lead to segmentation fault.
     */
    display->initialized = EGL_FALSE;

    /* First, destroy any surface associated to the given display. Then
     * destroy the display connection itself */
    wlEglDestroyAllSurfaces(display);

    if (skipDestroy != EGL_TRUE || display->ownNativeDpy) {
        if (display->wlRegistry) {
            wl_registry_destroy(display->wlRegistry);
        }
        if (display->wlStreamDpy) {
            wl_eglstream_display_destroy(display->wlStreamDpy);
        }

        /* Invalidate all event queues created for this display */
        wl_list_for_each_safe(iter, tmp, &display->evtQueueList, dpyLink) {
            wl_event_queue_destroy(iter->queue);
            iter->queue = NULL;
            wl_list_remove(&iter->dpyLink);
        }

        /* Invalidate and destroy event queues from dangling queue list if any */
        wl_list_for_each_safe(iter, tmp, &display->dangEvtQueueList, dangLink) {
            wl_event_queue_destroy(iter->queue);
            iter->queue = NULL;
            wl_list_remove(&iter->dangLink);
            free(iter);
        }
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
        }
    }


    /* XXX: Currently, we assume an internal EGLDisplay will only be used by a
     *      single external platform. In practice, we could create multiple
     *      external displays from different external implementations using the
     *      same internal EGLDisplay. This is a known issue currently tracked in
     *      the following Khronos bug:
     *
     *       https://cvs.khronos.org/bugzilla/show_bug.cgi?id=12072
     */
    if (fullTerminate) {
        res = data->egl.terminate(dpy);
    }

    return res;
}

EGLBoolean wlEglTerminateHook(EGLDisplay dpy)
{
    EGLBoolean res;

    wlExternalApiLock();
    res = terminateDisplay(dpy, EGL_FALSE);
    wlExternalApiUnlock();

    return res;
}

static EGLBoolean serverSupportsEglStream(struct wl_display *nativeDpy)
{
    struct wl_display     *wrapper      = NULL;
    struct wl_registry    *wlRegistry   = NULL;
    struct wl_event_queue *queue        = wl_display_create_queue(nativeDpy);
    int                    ret          = 0;
    EGLBoolean             hasEglStream = EGL_FALSE;
    const struct wl_registry_listener registryListener = {
        registry_handle_global_check_eglstream,
        registry_handle_global_remove
    };

    if (queue == NULL) {
        return EGL_FALSE;
    }

    wrapper = wl_proxy_create_wrapper(nativeDpy);
    wl_proxy_set_queue((struct wl_proxy *)wrapper, queue);

    /* Listen to wl_registry events and make a roundtrip in order to find the
     * wl_eglstream_display global object.
     */
    wlRegistry = wl_display_get_registry(wrapper);
    wl_proxy_wrapper_destroy(wrapper); /* Done with wrapper */
    ret = wl_registry_add_listener(wlRegistry,
                                   &registryListener,
                                   &hasEglStream);
    if (ret == 0) {
        wl_display_roundtrip_queue(nativeDpy, queue);
    }

    if (queue) {
        wl_event_queue_destroy(queue);
    }
    if (wlRegistry) {
       wl_registry_destroy(wlRegistry);
    }

    return hasEglStream;
}

EGLDisplay wlEglGetPlatformDisplayExport(void *data,
                                         EGLenum platform,
                                         void *nativeDpy,
                                         const EGLAttrib *attribs)
{
    WlEglPlatformData     *pData        = (WlEglPlatformData *)data;
    WlEglDeviceDpy        *devDpy       = NULL;
    WlEglDisplay          *display      = NULL;
    EGLint                 numDevices   = 0;
    int                    nAttribs     = 0;
    EGLint                *attribs2     = NULL;
    int                    i            = 0;
    EGLDisplay             eglDisplay   = NULL;
    EGLDeviceEXT           eglDevice    = NULL;
    EGLint                 err          = EGL_SUCCESS;

    if (platform != EGL_PLATFORM_WAYLAND_EXT) {
        wlEglSetError(data, EGL_BAD_PARAMETER);
        return EGL_NO_DISPLAY;
    }

    if (!pData->egl.queryDevices(1, &eglDevice, &numDevices) || numDevices == 0) {
        goto fail;
    }

    /* We need to convert EGLAttrib style attributes to EGLint style attributes
       before calling eglGetPlatformDisplayEXT which takes an EGLint* */

    if (attribs) {
        while (attribs[nAttribs] != EGL_NONE) {
            nAttribs += 2;
        }

        attribs2 = calloc(nAttribs + 1, sizeof(EGLint));
        if (!attribs2) {
            err = EGL_BAD_ALLOC;
            goto fail;
        }

        for (i = 0; i < nAttribs; i += 2) {
            attribs2[i] = (EGLint) attribs[i];
            attribs2[i+1] = (EGLint) attribs[i+1];
        }

        attribs2[nAttribs] = EGL_NONE;
    }

    eglDisplay = pData->egl.getPlatformDisplay(EGL_PLATFORM_DEVICE_EXT,
                                               eglDevice,
                                               attribs2);
    free(attribs2);
    if (eglDisplay == EGL_NO_DISPLAY) {
        goto fail;
    }

    wlExternalApiLock();

    wl_list_for_each(display, &wlEglDisplayList, link) {
        if ((display->nativeDpy == nativeDpy ||
            (!nativeDpy && display->ownNativeDpy))
            && display->devDpy->eglDisplay == eglDisplay) {
            wlExternalApiUnlock();
            return (EGLDisplay)display;
        }
    }

    display = calloc(1, sizeof(*display));
    if (!display) {
        wlExternalApiUnlock();
        err = EGL_BAD_ALLOC;
        goto fail;
    }

    wl_list_init(&display->evtQueueList);
    wl_list_init(&display->dangEvtQueueList);

    display->data = pData;

    display->initialized = EGL_FALSE;
    display->nativeDpy   = nativeDpy;

    /* If default display is requested, create a new Wayland display connection
     * and its corresponding internal EGLDisplay. Otherwise, check for existing
     * associated EGLDisplay for the given Wayland display and if it doesn't
     * exist, create a new one
     */
    if (!display->nativeDpy) {
        display->nativeDpy = wl_display_connect(NULL);
        if (!display->nativeDpy) {
            wlExternalApiUnlock();
            err = EGL_BAD_ALLOC;
            goto fail;
        }

        display->ownNativeDpy = EGL_TRUE;
        wl_display_dispatch_pending(display->nativeDpy);
    }

    if (!serverSupportsEglStream(display->nativeDpy)) {
        wlExternalApiUnlock();
        goto fail;
    }

    /*
     * Seek for the desired device in wlEglDeviceDpyList. If found, use it;
     * otherwise, create a new WlEglDeviceDpy and add it to wlEglDeviceDpyList
     */
    wl_list_for_each(devDpy, &wlEglDeviceDpyList, link) {
        /* TODO: Add support for multiple devices/device selection */
        if (devDpy->eglDisplay == eglDisplay) {
            display->devDpy = devDpy;
            display->devDpy->refCount++;
            break;
        }
    }

    /* If no device found in wlEglDeviceDpyList, insert the newly created
     * WlEglDeviceDpy into wlEglDeviceDpyList.
     */
    if (!display->devDpy) {
        devDpy = calloc(1, sizeof(*devDpy));
        if (!devDpy) {
            wlExternalApiUnlock();
            err = EGL_BAD_ALLOC;
            goto fail;
        }

        devDpy->eglDevice = eglDevice;
        devDpy->eglDisplay = eglDisplay;

        wl_list_insert(&wlEglDeviceDpyList, &devDpy->link);
        display->devDpy = devDpy;

        display->devDpy->refCount++;
    }

    // The newly created WlEglDisplay has been set up properly, insert it
    // in wlEglDisplayList.
    wl_list_insert(&wlEglDisplayList, &display->link);

    wlExternalApiUnlock();
    return display;

fail:

    if (display->ownNativeDpy) {
        wl_display_disconnect(display->nativeDpy);
    }
    free(display);
    free(devDpy);

    if (err != EGL_SUCCESS) {
        wlEglSetError(data, err);
    }

    return EGL_NO_DISPLAY;
}

EGLBoolean wlEglInitializeHook(EGLDisplay dpy, EGLint *major, EGLint *minor)
{
    WlEglDisplay      *display     = (WlEglDisplay *)dpy;
    WlEglPlatformData *data        = display->data;
    EGLBoolean         res         = EGL_FALSE;
    EGLAttrib          useRefCount = EGL_FALSE;
    struct wl_display     *wrapper      = NULL;
    struct wl_event_queue *queue        = NULL;
    EGLint                 err          = EGL_SUCCESS;
    int                    ret          = 0;

    wlExternalApiLock();

    if (display->initialized) {
        wlExternalApiUnlock();
        return EGL_TRUE;
    }

    dpy = display->devDpy->eglDisplay;
    res = data->egl.initialize(dpy, major, minor);

    if (res) {
        const char *exts = data->egl.queryString(dpy, EGL_EXTENSIONS);

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
        CACHE_EXT(NV,  stream_flush);
        CACHE_EXT(KHR, display_reference);

#undef CACHE_EXT

    }

    if (display->exts.display_reference) {
        data->egl.queryDisplayAttrib(dpy, EGL_TRACK_REFERENCES_KHR, &useRefCount);
        display->useRefCount = (EGLBoolean) useRefCount;
    }

    if (display->useRefCount) {
        display->refCount += 1;
    }

    queue = wlGetEventQueue(display);
    if (queue == NULL) {
        err = EGL_BAD_ALLOC;
        goto fail;
    }

    wrapper = wl_proxy_create_wrapper(display->nativeDpy);
    wl_proxy_set_queue((struct wl_proxy *)wrapper, queue);

    /* Listen to wl_registry events and make a roundtrip in order to find the
     * wl_eglstream_display global object
     */
    display->wlRegistry = wl_display_get_registry(wrapper);
    wl_proxy_wrapper_destroy(wrapper); /* Done with wrapper */
    ret = wl_registry_add_listener(display->wlRegistry,
                                   &registry_listener,
                                   display);
    if (ret == 0) {
        ret = wlEglRoundtrip(display, queue, EGL_FALSE);
    }
    if (ret < 0 || !display->wlStreamDpy) {
        err = EGL_BAD_ALLOC;
        goto fail;
    }

    /* Listen to wl_eglstream_display events and make another roundtrip so we
     * catch any bind-related event (e.g. server capabilities)
     */
    ret = wl_eglstream_display_add_listener(display->wlStreamDpy,
                                            &eglstream_display_listener,
                                            display);
    if (ret == 0) {
        ret = wlEglRoundtrip(display, queue, EGL_FALSE);
    }
    if (ret < 0) {
        err = EGL_BAD_ALLOC;
        goto fail;
    }

    display->initialized = EGL_TRUE;
    wlExternalApiUnlock();
    return EGL_TRUE;

fail:
    terminateDisplay(display, EGL_FALSE);
    if (err != EGL_SUCCESS) {
        wlEglSetError(data, err);
    }
    wlExternalApiUnlock();
    return EGL_FALSE;
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
    WlEglDisplay      *display       = (WlEglDisplay *)dpy;
    WlEglPlatformData *data          = display->data;
    EGLint            *attribs2      = NULL;
    EGLint             nAttribs      = 0;
    EGLint             nTotalAttribs = 0;
    EGLBoolean         surfType      = EGL_FALSE;
    EGLint             err           = EGL_SUCCESS;
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
    nTotalAttribs += (surfType ? nAttribs : (nAttribs + 2));

    /* Make attributes list copy */
    attribs2 = (EGLint *)malloc((nTotalAttribs + 1) * sizeof(*attribs2));
    if (!attribs2) {
        err = EGL_BAD_ALLOC;
        goto done;
    }

    if (nAttribs > 0) {
        memcpy(attribs2, attribs, nAttribs * sizeof(*attribs2));
    }
    attribs2[nTotalAttribs] = EGL_NONE;

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
        attribs2[nTotalAttribs - 2] = EGL_SURFACE_TYPE;
        attribs2[nTotalAttribs - 1] = EGL_STREAM_BIT_KHR;
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
    WlEglDeviceDpy *devDpy, *nextDevDpy;

    EGLBoolean res = EGL_TRUE;

    wlExternalApiLock();

    wl_list_for_each_safe(display, next, &wlEglDisplayList, link) {
        if (display->data == data) {
            res = terminateDisplay((EGLDisplay)display, EGL_TRUE) && res;
            if (display->ownNativeDpy) {
                wl_display_disconnect(display->nativeDpy);
            }
            wl_list_remove(&display->link);
            /* Destroy the external display */
            free(display);
        }
    }

    wl_list_for_each_safe(devDpy, nextDevDpy, &wlEglDeviceDpyList, link) {
        wl_list_remove(&devDpy->link);
        free(devDpy);

    }
    wlExternalApiUnlock();

    return res;
}

const char* wlEglQueryStringExport(void *data,
                                   EGLDisplay dpy,
                                   EGLExtPlatformString name)
{
    WlEglPlatformData *pData   = (WlEglPlatformData *)data;
    EGLBoolean         isEGL15 = (pData->egl.major > 1) ||
                                 ((pData->egl.major == 1) &&
                                  (pData->egl.minor >= 5));
    const char        *res     = NULL;

    switch (name) {
    case EGL_EXT_PLATFORM_PLATFORM_CLIENT_EXTENSIONS:
        res = isEGL15 ? "EGL_KHR_platform_wayland EGL_EXT_platform_wayland" :
                        "EGL_EXT_platform_wayland";
        break;

    case EGL_EXT_PLATFORM_DISPLAY_EXTENSIONS:
        if (dpy == EGL_NO_DISPLAY) {
            /* This should return all client extensions, which for now is
             * equivalent to EXTERNAL_PLATFORM_CLIENT_EXTENSIONS */
            res = isEGL15 ? "EGL_KHR_platform_wayland EGL_EXT_platform_wayland" :
                            "EGL_EXT_platform_wayland";
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
        break;

    default:
        break;
    }

    return res;
}
