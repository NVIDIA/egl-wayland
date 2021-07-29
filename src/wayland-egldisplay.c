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
#include "linux-dmabuf-unstable-v1-client-protocol.h"
#include "wayland-eglstream-server.h"
#include "wayland-thread.h"
#include "wayland-eglsurface.h"
#include "wayland-eglhandle.h"
#include "wayland-eglutils.h"
#include <string.h>
#include <stdlib.h>
#include <assert.h>

typedef struct WlServerProtocolsRec {
    EGLBoolean hasEglStream;
    EGLBoolean hasDmaBuf;
} WlServerProtocols;

/* TODO: Make global display lists hang off platform data */
static struct wl_list wlEglDisplayList = WL_LIST_INITIALIZER(&wlEglDisplayList);

EGLBoolean wlEglIsWaylandDisplay(void *nativeDpy)
{
    if (!wlEglMemoryIsReadable(nativeDpy, sizeof (void *))) {
        return EGL_FALSE;
    }

    return wlEglCheckInterfaceType(nativeDpy, "wl_display");
}

EGLBoolean wlEglIsValidNativeDisplayExport(void *data, void *nativeDpy)
{
    char       *val      = getenv("EGL_PLATFORM");
    (void)data;

    if (val && !strcasecmp(val, "wayland")) {
        return EGL_TRUE;
    }

    return wlEglIsWaylandDisplay(nativeDpy);
}

EGLBoolean wlEglBindDisplaysHook(void *data, EGLDisplay dpy, void *nativeDpy)
{
    /* Retrieve extension string before taking external API lock */
    const char *exts = ((WlEglPlatformData *)data)->egl.queryString(dpy, EGL_EXTENSIONS);
    EGLBoolean res = EGL_FALSE;

    wlExternalApiLock();

    res = wl_eglstream_display_bind((WlEglPlatformData *)data,
                                    (struct wl_display *)nativeDpy,
                                    dpy, exts);

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
dmabuf_handle_format(void *data,
                     struct zwp_linux_dmabuf_v1 *dmabuf,
                     uint32_t format)
{
    (void)data;
    (void)dmabuf;
    (void)format;
    /* Only use formats that include an associated modifier */
}

static void
dmabuf_add_format_modifier(WlEglDmaBufFormat *format, const uint64_t modifier)
{
    uint64_t *newModifiers;
    uint32_t m;

    for (m = 0; m < format->numModifiers; m++) {
        if (format->modifiers[m] == modifier) {
            return;
        }
    }

    newModifiers = realloc(format->modifiers,
                           sizeof(format->modifiers[0]) *
                           (format->numModifiers + 1));

    if (!newModifiers) {
        return;
    }

    newModifiers[format->numModifiers] = modifier;

    format->modifiers = newModifiers;
    format->numModifiers++;
}

static void
dmabuf_handle_modifier(void *data,
                       struct zwp_linux_dmabuf_v1 *dmabuf,
                       uint32_t format,
                       uint32_t mod_hi,
                       uint32_t mod_lo)
{
    WlEglDisplay *display = data;
    WlEglDmaBufFormat *newFormats;
    const uint64_t modifier = ((uint64_t)mod_hi << 32ULL) | (uint64_t)mod_lo;
    uint32_t f;

    (void)dmabuf;

    for (f = 0; f < display->numFormats; f++) {
        if (display->dmaBufFormats[f].format == format) {
            dmabuf_add_format_modifier(&display->dmaBufFormats[f], modifier);
            return;
        }
    }

    newFormats = realloc(display->dmaBufFormats,
                         sizeof(display->dmaBufFormats[0]) *
                         (display->numFormats + 1));

    if (!newFormats) {
        return;
    }

    newFormats[display->numFormats].format = format;
    newFormats[display->numFormats].numModifiers = 0;
    newFormats[display->numFormats].modifiers = NULL;
    dmabuf_add_format_modifier(&newFormats[display->numFormats], modifier);

    display->dmaBufFormats = newFormats;
    display->numFormats++;
}

static const struct zwp_linux_dmabuf_v1_listener dmabuf_listener = {
    .format = dmabuf_handle_format,
    .modifier = dmabuf_handle_modifier,
};

static void
dmabuf_set_interface(WlEglDisplay *display,
                     struct wl_registry *registry,
                     uint32_t name,
                     uint32_t version)
{
    if (version < 3) {
        /*
         * Version 3 added format modifier support, which the dmabuf
         * support in this library relies on.
         */
        return;
    }

    display->wlDmaBuf = wl_registry_bind(registry,
                                         name,
                                         &zwp_linux_dmabuf_v1_interface,
                                         3);
    zwp_linux_dmabuf_v1_add_listener(display->wlDmaBuf,
                                     &dmabuf_listener,
                                     display);
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
    } else if (strcmp(interface, "wl_eglstream_controller") == 0) {
        display->wlStreamCtl = wl_registry_bind(registry,
                                                name,
                                                &wl_eglstream_controller_interface,
                                                version);
        display->wlStreamCtlVer = version;
    } else if (strcmp(interface, "zwp_linux_dmabuf_v1") == 0) {
        dmabuf_set_interface(display, registry, name, version);
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
registry_handle_global_check_protocols(
                       void *data,
                       struct wl_registry *registry,
                       uint32_t name,
                       const char *interface,
                       uint32_t version)
{
    WlServerProtocols *protocols = (WlServerProtocols *)data;
    (void) registry;
    (void) name;
    (void) version;

    if (strcmp(interface, "wl_eglstream_display") == 0) {
        protocols->hasEglStream = EGL_TRUE;
    }

    if ((strcmp(interface, "zwp_linux_dmabuf_v1") == 0) &&
        (version >= 3)) {
        protocols->hasDmaBuf = EGL_TRUE;
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
    WlEglDisplay *dpy = (WlEglDisplay *)data;
    WlEglSurface *surf = NULL;
    (void) wlStreamDpy;

    wl_list_for_each(surf, &dpy->wlEglSurfaceList, link) {
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
static EGLBoolean terminateDisplay(WlEglDisplay *display, EGLBoolean globalTeardown)
{
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
    WlEglDisplay *display = wlEglAcquireDisplay(dpy);
    EGLBoolean res;

    if (!display) {
        return EGL_FALSE;
    }
    pthread_mutex_lock(&display->mutex);
    res = terminateDisplay(display, EGL_FALSE);
    pthread_mutex_unlock(&display->mutex);
    wlEglReleaseDisplay(display);

    return res;
}

static void checkServerProtocols(struct wl_display *nativeDpy,
                                 WlServerProtocols *protocols)
{
    struct wl_display     *wrapper      = NULL;
    struct wl_registry    *wlRegistry   = NULL;
    struct wl_event_queue *queue        = wl_display_create_queue(nativeDpy);
    int                    ret          = 0;
    const struct wl_registry_listener registryListener = {
        registry_handle_global_check_protocols,
        registry_handle_global_remove
    };

    if (queue == NULL) {
        return;
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
                                   protocols);
    if (ret == 0) {
        wl_display_roundtrip_queue(nativeDpy, queue);
    }

    if (queue) {
        wl_event_queue_destroy(queue);
    }
    if (wlRegistry) {
       wl_registry_destroy(wlRegistry);
    }
}

EGLDisplay wlEglGetPlatformDisplayExport(void *data,
                                         EGLenum platform,
                                         void *nativeDpy,
                                         const EGLAttrib *attribs)
{
    WlEglPlatformData     *pData           = (WlEglPlatformData *)data;
    WlEglDisplay          *display         = NULL;
    WlServerProtocols      protocols;
    EGLint                 numDevices      = 0;
    int                    i               = 0;
    EGLDeviceEXT           eglDevice       = NULL;
    EGLint                 err             = EGL_SUCCESS;
    EGLBoolean             useInitRefCount = EGL_FALSE;

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
                    useInitRefCount = (EGLBoolean) attribs[i + 1];
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
            && display->useInitRefCount == useInitRefCount) {
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
    display->useInitRefCount = useInitRefCount;

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

    memset(&protocols, 0, sizeof(protocols));
    checkServerProtocols(display->nativeDpy, &protocols);

    if (!protocols.hasEglStream && !protocols.hasDmaBuf) {
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

    if (!wlEglInitializeMutex(&display->mutex)) {
        wlExternalApiUnlock();
        goto fail;
    }
    display->refCount = 1;
    WL_LIST_INIT(&display->wlEglSurfaceList);


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
    WlEglDisplay      *display = wlEglAcquireDisplay(dpy);
    WlEglPlatformData *data    = NULL;
    struct wl_display *wrapper = NULL;
    EGLint             err     = EGL_SUCCESS;
    int                ret     = 0;

    if (!display) {
        return EGL_FALSE;
    }
    pthread_mutex_lock(&display->mutex);

    data = display->data;

    if (display->initCount > 0) {
        // This display has already been initialized.
        if (major) {
                *major = display->devDpy->major;
        }
        if (minor) {
                *minor = display->devDpy->minor;
        }
        if (display->useInitRefCount) {
            display->initCount++;
        }
        pthread_mutex_unlock(&display->mutex);
        wlEglReleaseDisplay(display);
        return EGL_TRUE;
    }

    if (!wlInternalInitialize(display->devDpy)) {
        pthread_mutex_unlock(&display->mutex);
        wlEglReleaseDisplay(display);
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
     * wl_eglstream_display and/or zwp_linux_dmabuf_v1 global object
     */
    display->wlRegistry = wl_display_get_registry(wrapper);
    wl_proxy_wrapper_destroy(wrapper); /* Done with wrapper */
    ret = wl_registry_add_listener(display->wlRegistry,
                                   &registry_listener,
                                   display);
    if (ret == 0) {
        ret = wl_display_roundtrip_queue(display->nativeDpy, display->wlEventQueue);
    }
    if (ret < 0) {
        err = EGL_BAD_ALLOC;
        goto fail;
    }

    if (display->wlStreamDpy) {
        /* Listen to wl_eglstream_display events and make another roundtrip so we
         * catch any bind-related event (e.g. server capabilities)
         */
        ret = wl_eglstream_display_add_listener(display->wlStreamDpy,
                                                &eglstream_display_listener,
                                                display);
        if (ret == 0) {
            ret = wl_display_roundtrip_queue(display->nativeDpy,
                                             display->wlEventQueue);
        }
        if (ret < 0) {
            err = EGL_BAD_ALLOC;
            goto fail;
        }
    } else if (!display->wlDmaBuf) {
        /* This library requires either the EGLStream or dma-buf protocols to
         * present content to the Wayland compositor.
         */
        err = EGL_BAD_ALLOC;
        goto fail;
    }

    if (major != NULL) {
        *major = display->devDpy->major;
    }
    if (minor != NULL) {
        *minor = display->devDpy->minor;
    }

    pthread_mutex_unlock(&display->mutex);
    wlEglReleaseDisplay(display);
    return EGL_TRUE;

fail:
    terminateDisplay(display, EGL_FALSE);
    if (err != EGL_SUCCESS) {
        wlEglSetError(data, err);
    }
    pthread_mutex_unlock(&display->mutex);
    wlEglReleaseDisplay(display);
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

WlEglDisplay *wlEglAcquireDisplay(EGLDisplay dpy) {
    WlEglDisplay *display = (WlEglDisplay *)dpy;
    wlExternalApiLock();
    if (wlEglIsWlEglDisplay(display)) {
        ++display->refCount;
    } else {
        display = NULL;
    }
    wlExternalApiUnlock();
    return display;
}

static void wlEglUnrefDisplay(WlEglDisplay *display) {
    if (--display->refCount == 0) {
        wlEglMutexDestroy(&display->mutex);
        free(display);
    }
}

void wlEglReleaseDisplay(WlEglDisplay *display) {
    wlExternalApiLock();
    wlEglUnrefDisplay(display);
    wlExternalApiUnlock();
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
    WlEglDisplay *display = wlEglAcquireDisplay(dpy);
    WlEglPlatformData *data = NULL;
    EGLBoolean ret = EGL_TRUE;

    if (!display) {
        return EGL_FALSE;
    }
    pthread_mutex_lock(&display->mutex);

    data = display->data;

    if (value == NULL) {
        wlEglSetError(data, EGL_BAD_PARAMETER);
        pthread_mutex_unlock(&display->mutex);
        wlEglReleaseDisplay(display);
        return EGL_FALSE;
    }

    if (display->initCount == 0) {
        wlEglSetError(data, EGL_NOT_INITIALIZED);
        pthread_mutex_unlock(&display->mutex);
        wlEglReleaseDisplay(display);
        return EGL_FALSE;
    }

    switch (name) {
    case EGL_DEVICE_EXT:
        *value = (EGLAttrib) display->devDpy->eglDevice;
        break;
    case EGL_TRACK_REFERENCES_KHR:
        *value = (EGLAttrib) display->useInitRefCount;
        break;
    default:
        ret = data->egl.queryDisplayAttrib(display->devDpy->eglDisplay, name, value);
        break;
    }

    pthread_mutex_unlock(&display->mutex);
    wlEglReleaseDisplay(display);
    return ret;
}

EGLBoolean wlEglDestroyAllDisplays(WlEglPlatformData *data)
{
    WlEglDisplay *display, *next;

    EGLBoolean res = EGL_TRUE;

    wlExternalApiLock();

    wl_list_for_each_safe(display, next, &wlEglDisplayList, link) {
        if (display->data == data) {
            pthread_mutex_lock(&display->mutex);
            res = terminateDisplay(display, EGL_TRUE) && res;
            if (display->ownNativeDpy) {
                wl_display_disconnect(display->nativeDpy);
            }
            display->devDpy = NULL;
            pthread_mutex_unlock(&display->mutex);
            wl_list_remove(&display->link);
            /* Unref the external display */
            wlEglUnrefDisplay(display);
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
             *
             * For Wayland support via dma-buf, at least the following
             * extensions must be supported by the underlying driver:
             *
             *  - EGL_KHR_stream
             *  - EGL_KHR_stream_producer_eglsurface
             *  - EGL_NV_stream_consumer_eglimage
             *  - EGL_MESA_image_dma_buf_export
             */
            const char *exts = pData->egl.queryString(dpy, EGL_EXTENSIONS);

            if (wlEglFindExtension("EGL_KHR_stream", exts) &&
                wlEglFindExtension("EGL_KHR_stream_producer_eglsurface",
                                   exts)) {
                if (wlEglFindExtension("EGL_KHR_stream_cross_process_fd",
                                       exts)) {
                    res = "EGL_WL_bind_wayland_display "
                        "EGL_WL_wayland_eglstream";
                } else if (wlEglFindExtension("EGL_NV_stream_consumer_eglimage",
                                              exts) &&
                           wlEglFindExtension("EGL_MESA_image_dma_buf_export",
                                              exts)) {
                    res = "EGL_WL_bind_wayland_display";
                }
            }
        }
        break;

    default:
        break;
    }

    return res;
}
