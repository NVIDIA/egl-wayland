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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <unistd.h>
#include <assert.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>

#include <wayland-server.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

#include "wayland-eglstream-server.h"
#include "wayland-eglstream-server-protocol.h"
#include "wayland-eglstream.h"
#include "wayland-drm.h"
#include "wayland-eglswap.h"
#include "wayland-eglutils.h"
#include "wayland-thread.h"

#define MASK(_VAL_) (1 << (_VAL_))

static struct wl_list wlStreamDpyList = WL_LIST_INITIALIZER(&wlStreamDpyList);

static void
destroy_wl_eglstream_resource(struct wl_resource *resource)
{
    struct wl_eglstream *wlStream = wl_resource_get_user_data(resource);

    if (wlStream->handle >= 0) {
        close(wlStream->handle);
    }

    free(wlStream);
}

static void
destroy_wl_eglstream(struct wl_client *client, struct wl_resource *resource)
{
    (void) client;
    wl_resource_destroy(resource);
}

static void
handle_create_stream(struct wl_client *client,
                     struct wl_resource *resource, uint32_t id,
                     int32_t width, int32_t height,
                     int handle, int handle_type,
                     struct wl_array *attribs)
{
    struct wl_eglstream_display *wlStreamDpy =
        wl_resource_get_user_data(resource);
    struct wl_eglstream *wlStream;
    struct sockaddr_in sockAddr;
    char sockAddrStr[NI_MAXHOST];
    intptr_t *attr;
    int mask = 0;
    enum wl_eglstream_error err;

    wlStream = calloc(1, sizeof *wlStream);
    if (wlStream == NULL) {
        err = WL_EGLSTREAM_ERROR_BAD_ALLOC;
        goto error_create_stream;
    }

    wlStream->wlStreamDpy = wlStreamDpy;
    wlStream->eglStream = EGL_NO_STREAM_KHR;
    wlStream->width = width;
    wlStream->height = height;
    wlStream->handle = -1;
    wlStream->yInverted = EGL_FALSE;

    memset(&sockAddr, 0, sizeof(sockAddr));

    switch (handle_type) {
        case WL_EGLSTREAM_HANDLE_TYPE_FD:
            wlStream->handle = handle;
            wlStream->fromFd = EGL_TRUE;
            break;

        case WL_EGLSTREAM_HANDLE_TYPE_INET:
            sockAddr.sin_family = AF_INET;
            wlStream->isInet    = EGL_TRUE;
            /* Close the given dummy fd */
            close(handle);
            break;

        case WL_EGLSTREAM_HANDLE_TYPE_SOCKET:
            wlStream->handle = handle;
            break;

        default:
            err = WL_EGLSTREAM_ERROR_BAD_HANDLE;
            goto error_create_stream;
    }

    wl_array_for_each(attr, attribs) {
        switch (attr[0]) {
            case WL_EGLSTREAM_ATTRIB_INET_ADDR:
                /* INET_ADDR should only be set once */
                if (mask & MASK(WL_EGLSTREAM_ATTRIB_INET_ADDR)) {
                    err = WL_EGLSTREAM_ERROR_BAD_ATTRIBS;
                    goto error_create_stream;
                }
                sockAddr.sin_addr.s_addr = htonl((int)attr[1]);
                mask |= MASK(WL_EGLSTREAM_ATTRIB_INET_ADDR);
                break;

            case WL_EGLSTREAM_ATTRIB_INET_PORT:
                /* INET_PORT should only be set once */
                if (mask & MASK(WL_EGLSTREAM_ATTRIB_INET_PORT)) {
                    err = WL_EGLSTREAM_ERROR_BAD_ATTRIBS;
                    goto error_create_stream;
                }
                sockAddr.sin_port = htons((int)attr[1]);
                mask |= MASK(WL_EGLSTREAM_ATTRIB_INET_PORT);
                break;

            case WL_EGLSTREAM_ATTRIB_Y_INVERTED:
                /* Y_INVERTED should only be set once */
                if (mask & MASK(WL_EGLSTREAM_ATTRIB_Y_INVERTED)) {
                    err = WL_EGLSTREAM_ERROR_BAD_ATTRIBS;
                    goto error_create_stream;
                }
                wlStream->yInverted = (EGLBoolean)attr[1];
                mask |= MASK(WL_EGLSTREAM_ATTRIB_Y_INVERTED);
                break;

            default:
                assert(!"Unknown attribute");
                break;
        }

        /* Attribs processed in pairs */
        attr++;
    }

    if (wlStream->isInet) {
        /* Both address and port should have been set */
        if (mask != (MASK(WL_EGLSTREAM_ATTRIB_INET_ADDR) |
                     MASK(WL_EGLSTREAM_ATTRIB_INET_PORT))) {
            err = WL_EGLSTREAM_ERROR_BAD_ATTRIBS;
            goto error_create_stream;
        }

        wlStream->handle = socket(AF_INET, SOCK_STREAM, 0);
        if (wlStream->handle == -1) {
            err = WL_EGLSTREAM_ERROR_BAD_ALLOC;
            goto error_create_stream;
        }

        if (connect(wlStream->handle,
                    (struct sockaddr *)&sockAddr,
                    sizeof(sockAddr)) < 0) {
            err = WL_EGLSTREAM_ERROR_BAD_ADDRESS;
            goto error_create_stream;
        }
    }

    wlStream->resource =
        wl_resource_create(client, &wl_buffer_interface, 1, id);
    if (!wlStream->resource) {
        err = WL_EGLSTREAM_ERROR_BAD_ALLOC;
        goto error_create_stream;
    }

    wl_resource_set_implementation(
                        wlStream->resource,
                        (void (**)(void))&wlStreamDpy->wl_eglstream_interface,
                        wlStream,
                        destroy_wl_eglstream_resource);
    return;

error_create_stream:
    switch (err) {
        case WL_EGLSTREAM_ERROR_BAD_ALLOC:
            wl_resource_post_no_memory(resource);
            break;
        case WL_EGLSTREAM_ERROR_BAD_HANDLE:
            wl_resource_post_error(resource, err, "Invalid or unknown handle");
            break;
        case WL_EGLSTREAM_ERROR_BAD_ATTRIBS:
            wl_resource_post_error(resource, err, "Malformed attributes list");
            break;
        case WL_EGLSTREAM_ERROR_BAD_ADDRESS:
            wl_resource_post_error(resource, err, "Unable to connect to %s:%d.",
                                   (getnameinfo((struct sockaddr *)&sockAddr,
                                                sizeof(sockAddr),
                                                sockAddrStr, NI_MAXHOST,
                                                NULL, 0,
                                                NI_NUMERICHOST) ?
                                    "<invalid IP>" : sockAddrStr),
                                   ntohs(sockAddr.sin_port));
            break;
        default:
            assert(!"Unknown error code");
            break;
    }

    if (wlStream) {
        if (wlStream->isInet && wlStream->handle >= 0) {
            close(wlStream->handle);
        }
        free(wlStream);
    }
}

static void
handle_swap_interval(struct wl_client *client,
                     struct wl_resource *displayResource,
                     struct wl_resource *streamResource,
                     int interval)
{
    struct wl_eglstream_display *wlStreamDpy =
        wl_resource_get_user_data(displayResource);
    struct wl_eglstream *wlStream =
        wl_eglstream_display_get_stream(wlStreamDpy, streamResource);
    (void) client;

    if (wlEglStreamSwapIntervalCallback(wlStreamDpy->data,
                                        wlStream->eglStream,
                                        &interval) == EGL_BAD_MATCH) {
        wl_eglstream_display_send_swapinterval_override(displayResource,
                                                        interval,
                                                        streamResource);
    }
}

static const struct wl_eglstream_display_interface
wl_eglstream_display_interface_impl = {
    handle_create_stream,
    handle_swap_interval,
};

static void
wl_eglstream_display_global_bind(struct wl_client *client,
                                 void *data,
                                 uint32_t version,
                                 uint32_t id)
{
    struct wl_eglstream_display *wlStreamDpy  = NULL;
    struct wl_resource          *resource     = NULL;

    wlStreamDpy = (struct wl_eglstream_display *)data;
    resource    = wl_resource_create(client,
                                     &wl_eglstream_display_interface,
                                     version,
                                     id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }

    wl_resource_set_implementation(resource,
                                   &wl_eglstream_display_interface_impl,
                                   data,
                                   NULL);


    wl_eglstream_display_send_caps(resource, wlStreamDpy->supported_caps);
}

EGLBoolean
wl_eglstream_display_bind(WlEglPlatformData *data,
                          struct wl_display *wlDisplay,
                          EGLDisplay eglDisplay,
                          const char *exts,
                          const char *dev_name)
{
    struct wl_eglstream_display *wlStreamDpy = NULL;
    char                        *env         = NULL;

    /* Check whether there's an EGLDisplay already bound to the given
     * wl_display */
    if (wl_eglstream_display_get(eglDisplay) != NULL) {
        return EGL_FALSE;
    }

    wlStreamDpy = calloc(1, sizeof(*wlStreamDpy));
    if (!wlStreamDpy) {
        return EGL_FALSE;
    }

    wlStreamDpy->data          = data;
    wlStreamDpy->wlDisplay     = wlDisplay;
    wlStreamDpy->eglDisplay    = eglDisplay;
    wlStreamDpy->caps_override = 0;

#define CACHE_EXT(_PREFIX_, _NAME_)                                      \
        wlStreamDpy->exts._NAME_ =                                       \
            !!wlEglFindExtension("EGL_" #_PREFIX_ "_" #_NAME_, exts)

    CACHE_EXT(NV,  stream_attrib);
    CACHE_EXT(KHR, stream_cross_process_fd);
    CACHE_EXT(NV,  stream_remote);
    CACHE_EXT(NV,  stream_socket);
    CACHE_EXT(NV,  stream_socket_inet);
    CACHE_EXT(NV,  stream_socket_unix);
    CACHE_EXT(NV,  stream_origin);

#undef CACHE_EXT

    /* Advertise server capabilities */
    if (wlStreamDpy->exts.stream_cross_process_fd) {
        wlStreamDpy->supported_caps |= WL_EGLSTREAM_DISPLAY_CAP_STREAM_FD;
    }
    if (wlStreamDpy->exts.stream_attrib &&
        wlStreamDpy->exts.stream_remote &&
        wlStreamDpy->exts.stream_socket) {
        if (wlStreamDpy->exts.stream_socket_inet) {
            wlStreamDpy->supported_caps |= WL_EGLSTREAM_DISPLAY_CAP_STREAM_INET;
        }
        if (wlStreamDpy->exts.stream_socket_unix) {
            wlStreamDpy->supported_caps |= WL_EGLSTREAM_DISPLAY_CAP_STREAM_SOCKET;
        }
    }

    env = getenv("WL_EGLSTREAM_CAP_OVERRIDE");
    if (env) {
        int serverCapOverride = atoi(env);
        wlStreamDpy->caps_override = (wlStreamDpy->supported_caps
                                      & serverCapOverride) !=
                                      wlStreamDpy->supported_caps;
        wlStreamDpy->supported_caps &= serverCapOverride;
    }

    wlStreamDpy->wl_eglstream_interface.destroy = destroy_wl_eglstream;
    wlStreamDpy->global = wl_global_create(wlDisplay,
                                           &wl_eglstream_display_interface, 1,
                                           wlStreamDpy,
                                           wl_eglstream_display_global_bind);

    /* Failure is not fatal */
    wl_drm_display_bind(wlDisplay, wlStreamDpy, dev_name);

    wl_list_insert(&wlStreamDpyList, &wlStreamDpy->link);

    return EGL_TRUE;
}

void
wl_eglstream_display_unbind(struct wl_eglstream_display *wlStreamDpy)
{
    wl_drm_display_unbind(wlStreamDpy);
    wl_global_destroy(wlStreamDpy->global);
    wl_list_remove(&wlStreamDpy->link);
    free(wlStreamDpy);
}

struct wl_eglstream_display* wl_eglstream_display_get(EGLDisplay eglDisplay)
{
    struct wl_eglstream_display *wlDisplay;

    wl_list_for_each(wlDisplay, &wlStreamDpyList, link) {
        if (wlDisplay->eglDisplay == eglDisplay) {
            return wlDisplay;
        }
    }

    return NULL;
}

struct wl_eglstream*
wl_eglstream_display_get_stream(struct wl_eglstream_display *wlStreamDpy,
                                struct wl_resource *resource)
{
    if (resource == NULL) {
        return NULL;
    }

    if (wl_resource_instance_of(resource, &wl_buffer_interface,
                                &wlStreamDpy->wl_eglstream_interface)) {
        return wl_resource_get_user_data(resource);
    } else {
        return NULL;
    }
}

