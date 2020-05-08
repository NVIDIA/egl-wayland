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

EGLBoolean wlEglIsWaylandDisplay(void *nativeDpy)
{
#if HAS_MINCORE
    if (!wlEglPointerIsDereferencable(nativeDpy)) {
        return EGL_FALSE;
    }

    return WL_CHECK_INTERFACE_TYPE(nativeDpy, wl_display_interface);
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
static EGLBoolean terminateDisplay(EGLDisplay dpy, EGLBoolean globalTeardown)
{
    WlEglDisplay      *display       = (WlEglDisplay *)dpy;

    if (display->initCount == 0) {
        return EGL_TRUE;
    }

    /* If globalTeardown is true, then ignore the refcount and terminate the
       display. That's used when the library is unloaded. */
    if (display->initCount > 1 && !globalTeardown) {
        display->initCount--;
        return EGL_TRUE;
    }

    if (!wlInternalTerminate(display->devDpy)) {
        if (!globalTeardown) {
            return EGL_FALSE;
        }
    }
    display->initCount = 0;

    /* First, destroy any surface associated to the given display. Then
     * destroy the display connection itself */
    wlEglDestroyAllSurfaces(display);

    if (!globalTeardown || display->ownNativeDpy) {
        if (display->wlRegistry) {
            wl_registry_destroy(display->wlRegistry);
            display->wlRegistry = NULL;
        }
        if (display->wlStreamDpy) {
            wl_eglstream_display_destroy(display->wlStreamDpy);
            display->wlStreamDpy = NULL;
        }
        if (display->wlEventQueue) {
            wl_event_queue_destroy(display->wlEventQueue);
            display->wlEventQueue = NULL;
        }
    }

    return EGL_TRUE;
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
    WlEglDisplay          *display      = NULL;
    EGLint                 numDevices   = 0;
    int                    i            = 0;
    EGLDeviceEXT           eglDevice    = NULL;
    EGLint                 err          = EGL_SUCCESS;
    EGLBoolean             useRefCount  = EGL_FALSE;

    if (platform != EGL_PLATFORM_WAYLAND_EXT) {
        wlEglSetError(data, EGL_BAD_PARAMETER);
        return EGL_NO_DISPLAY;
    }

    /* Check the attribute list. Any attributes are likely to require some
     * special handling, so reject anything we don't recognize.
     */
    if (attribs) {
        for (i = 0; attribs[i] != EGL_NONE; i += 2) {
            if (attribs[i] == EGL_TRACK_REFERENCES_KHR) {
                if (attribs[i + 1] == EGL_TRUE || attribs[i + 1] == EGL_FALSE) {
                    useRefCount = (EGLBoolean) attribs[i + 1];
                } else {
                    wlEglSetError(data, EGL_BAD_ATTRIBUTE);
                    return EGL_NO_DISPLAY;
                }
            } else {
                wlEglSetError(data, EGL_BAD_ATTRIBUTE);
                return EGL_NO_DISPLAY;
            }
        }
    }

    wlExternalApiLock();

    /* First, check if we've got an existing display that matches. */
    wl_list_for_each(display, &wlEglDisplayList, link) {
        if ((display->nativeDpy == nativeDpy ||
            (!nativeDpy && display->ownNativeDpy))
            && display->useRefCount == useRefCount) {
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

    display->data = pData;

    display->nativeDpy   = nativeDpy;
    display->useRefCount = useRefCount;

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

    if (!pData->egl.queryDevices(1, &eglDevice, &numDevices) || numDevices == 0) {
        wlExternalApiUnlock();
        goto fail;
    }
    display->devDpy = wlGetInternalDisplay(pData, eglDevice);
    if (display->devDpy == NULL) {
        wlExternalApiUnlock();
        goto fail;
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

    if (err != EGL_SUCCESS) {
        wlEglSetError(data, err);
    }

    return EGL_NO_DISPLAY;
}

EGLBoolean wlEglInitializeHook(EGLDisplay dpy, EGLint *major, EGLint *minor)
{
    WlEglDisplay      *display     = (WlEglDisplay *)dpy;
    WlEglPlatformData *data        = display->data;
    struct wl_display     *wrapper      = NULL;
    EGLint                 err          = EGL_SUCCESS;
    int                    ret          = 0;

    wlExternalApiLock();

    if (display->initCount > 0) {
        // This display has already been initialized.
        if (major) {
                *major = display->devDpy->major;
        }
        if (minor) {
                *minor = display->devDpy->minor;
        }
        if (display->useRefCount) {
            display->initCount++;
        }
        wlExternalApiUnlock();
        return EGL_TRUE;
    }

    if (!wlInternalInitialize(display->devDpy)) {
        wlExternalApiUnlock();
        return EGL_FALSE;
    }

    // Set the initCount to 1. If something goes wrong, then terminateDisplay
    // will clean up and set it back to zero.
    display->initCount = 1;

    display->wlEventQueue =  wl_display_create_queue(display->nativeDpy);;
    if (display->wlEventQueue == NULL) {
        err = EGL_BAD_ALLOC;
        goto fail;
    }

    wrapper = wl_proxy_create_wrapper(display->nativeDpy);
    wl_proxy_set_queue((struct wl_proxy *)wrapper, display->wlEventQueue);

    /* Listen to wl_registry events and make a roundtrip in order to find the
     * wl_eglstream_display global object
     */
    display->wlRegistry = wl_display_get_registry(wrapper);
    wl_proxy_wrapper_destroy(wrapper); /* Done with wrapper */
    ret = wl_registry_add_listener(display->wlRegistry,
                                   &registry_listener,
                                   display);
    if (ret == 0) {
        ret = wl_display_roundtrip_queue(display->nativeDpy, display->wlEventQueue);
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
        ret = wl_display_roundtrip_queue(display->nativeDpy, display->wlEventQueue);
    }
    if (ret < 0) {
        err = EGL_BAD_ALLOC;
        goto fail;
    }

    if (major != NULL) {
        *major = display->devDpy->major;
    }
    if (minor != NULL) {
        *minor = display->devDpy->minor;
    }

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

EGLBoolean wlEglQueryDisplayAttribHook(EGLDisplay dpy,
                                       EGLint name,
                                       EGLAttrib *value)
{
    WlEglDisplay *display = (WlEglDisplay *) dpy;
    WlEglPlatformData *data = display->data;
    EGLBoolean ret = EGL_TRUE;

    if (value == NULL) {
        wlEglSetError(data, EGL_BAD_PARAMETER);
        return EGL_FALSE;
    }

    wlExternalApiLock();

    if (display->initCount == 0) {
        wlEglSetError(data, EGL_NOT_INITIALIZED);
        wlExternalApiUnlock();
        return EGL_FALSE;
    }

    switch (name) {
    case EGL_DEVICE_EXT:
        *value = (EGLAttrib) display->devDpy->eglDevice;
        break;
    case EGL_TRACK_REFERENCES_KHR:
        *value = (EGLAttrib) display->useRefCount;
        break;
    default:
        ret = data->egl.queryDisplayAttrib(display->devDpy->eglDisplay, name, value);
        break;
    }

    wlExternalApiUnlock();
    return ret;
}

EGLBoolean wlEglDestroyAllDisplays(WlEglPlatformData *data)
{
    WlEglDisplay *display, *next;

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

    wlFreeAllInternalDisplays(data);

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
