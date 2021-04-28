/*
 * Copyright (c) 2016, NVIDIA CORPORATION. All rights reserved.
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

#include "wayland-eglstream.h"
#include "wayland-eglstream-server.h"
#include "wayland-thread.h"
#include "wayland-eglhandle.h"
#include "wayland-egldisplay.h"
#include "wayland-eglutils.h"
#include "wayland-egl-ext.h"
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>

#define WL_EGL_CONN_WAIT_USECS    1e3 /* 1 msec */
#define WL_EGL_CONN_TIMEOUT_USECS 1e6 /* 1 sec */

EGLStreamKHR wlEglCreateStreamAttribHook(EGLDisplay dpy,
                                         const EGLAttrib *attribs)
{
    WlEglPlatformData           *data        = NULL;
    EGLStreamKHR                 stream      = EGL_NO_STREAM_KHR;
    struct wl_eglstream_display *wlStreamDpy = NULL;
    struct wl_resource          *resource    = NULL;
    struct wl_eglstream         *wlStream    = NULL;
    int                          nAttribs    = 0;
    int                          idx         = 0;
    int                          fd          = -1;
    EGLint                       err         = EGL_SUCCESS;

    /* Parse attribute list and count internal attributes */
    if (attribs) {
        while (attribs[idx] != EGL_NONE) {
            if (attribs[idx] == EGL_WAYLAND_EGLSTREAM_WL) {
                if (resource != NULL) {
                    err =  EGL_BAD_MATCH;
                    break;
                }

                resource = (struct wl_resource *)attribs[idx + 1];
                if (resource == NULL) {
                    err = EGL_BAD_ACCESS;
                    break;
                }
            } else {
                /* Internal attribute */
                nAttribs++;
            }
            idx += 2;
        }
    }

    if ((err == EGL_SUCCESS) && (resource == NULL)) {
        /* No EGL_WAYLAND_EGLSTREAM_WL attribute provided, which means dpy is
         * external. Forward this call to the underlying driver as there's
         * nothing to do here */
        WlEglDisplay *display = (WlEglDisplay *)dpy;
        return display->data->egl.createStreamAttrib(display->devDpy->eglDisplay,
                                                     attribs);
    }

    /* Otherwise, we must create a server-side stream */
    wlExternalApiLock();

    wlStreamDpy = wl_eglstream_display_get(dpy);
    if (wlStreamDpy == NULL) {
        err = EGL_BAD_ACCESS;
    } else {
        data = wlStreamDpy->data;
    }

    if (err != EGL_SUCCESS) {
        goto fail;
    }

    wlStream = wl_eglstream_display_get_stream(wlStreamDpy, resource);
    if (wlStream == NULL) {
        err = EGL_BAD_ACCESS;
        goto fail;
    }

    if (wlStream->eglStream != EGL_NO_STREAM_KHR ||
        wlStream->handle == -1) {
        err = EGL_BAD_STREAM_KHR;
        goto fail;
    }

    if (wlStream->fromFd) {
        /* Check for EGL_KHR_stream_cross_process_fd support */
        if (!wlStreamDpy->exts.stream_cross_process_fd) {
            err = EGL_BAD_ACCESS;
            goto fail;
        }

        /* eglCreateStreamFromFileDescriptorKHR from
         * EGL_KHR_stream_cross_process_fd does not take attributes. Thus, only
         * EGL_WAYLAND_EGLSTREAM_WL should have been specified and processed
         * above. caps_override is an exception to this, since the wayland
         * compositor calling into this function wouldn't be aware of an
         * override in place */
        if (nAttribs != 0 && !wlStreamDpy->caps_override) {
            err = EGL_BAD_ATTRIBUTE;
            goto fail;
        }

        fd = wlStream->handle;
        stream = data->egl.createStreamFromFD(dpy, wlStream->handle);

        /* Clean up */
        close(fd);
        wlStream->handle = -1;
    }
#if defined(EGL_NV_stream_attrib) && \
    defined(EGL_NV_stream_remote) && \
    defined(EGL_NV_stream_socket)
    else {
        EGLAttrib *attribs2 = NULL;

        /* Check for required extensions support */
        if (!wlStreamDpy->exts.stream_attrib ||
            !wlStreamDpy->exts.stream_remote ||
            !wlStreamDpy->exts.stream_socket ||
            (!wlStreamDpy->exts.stream_socket_inet &&
             !wlStreamDpy->exts.stream_socket_unix)) {
            err = EGL_BAD_ACCESS;
            goto fail;
        }

        /* If not inet connection supported, wlStream should not be inet */
        if (!wlStreamDpy->exts.stream_socket_inet &&
            wlStream->isInet) {
            err = EGL_BAD_ACCESS;
            goto fail;
        }

        /* Create attributes array to pass down to the actual EGL stream
         * creation function */
        attribs2 = (EGLAttrib *)malloc((2*(nAttribs + 5) + 1)*sizeof(*attribs2));

        nAttribs = 0;
        attribs2[nAttribs++] = EGL_STREAM_TYPE_NV;
        attribs2[nAttribs++] = EGL_STREAM_CROSS_PROCESS_NV;
        attribs2[nAttribs++] = EGL_STREAM_PROTOCOL_NV;
        attribs2[nAttribs++] = EGL_STREAM_PROTOCOL_SOCKET_NV;
        attribs2[nAttribs++] = EGL_STREAM_ENDPOINT_NV;
        attribs2[nAttribs++] = EGL_STREAM_CONSUMER_NV;
        attribs2[nAttribs++] = EGL_SOCKET_TYPE_NV;
        attribs2[nAttribs++] = (wlStream->isInet ? EGL_SOCKET_TYPE_INET_NV :
                                                   EGL_SOCKET_TYPE_UNIX_NV);
        attribs2[nAttribs++] = EGL_SOCKET_HANDLE_NV;
        attribs2[nAttribs++] = (EGLAttrib)wlStream->handle;

        /* Include internal attributes given by the application */
        while (attribs && attribs[0] != EGL_NONE) {
            switch (attribs[0]) {
                /* Filter out external attributes */
                case EGL_WAYLAND_EGLSTREAM_WL:
                    break;

                /* EGL_NV_stream_remote attributes shouldn't be set by the
                 * application */
                case EGL_STREAM_TYPE_NV:
                case EGL_STREAM_PROTOCOL_NV:
                case EGL_STREAM_ENDPOINT_NV:
                case EGL_SOCKET_TYPE_NV:
                case EGL_SOCKET_HANDLE_NV:
                    free(attribs2);
                    err = EGL_BAD_ATTRIBUTE;
                    goto fail;

                /* Everything else is fine and will be handled by EGL */
                default:
                    attribs2[nAttribs++] = attribs[0];
                    attribs2[nAttribs++] = attribs[1];
            }

            attribs += 2;
        }
        attribs2[nAttribs] = EGL_NONE;

        stream = data->egl.createStreamAttrib(dpy, attribs2);

        /* Clean up */
        free(attribs2);

        if (stream != EGL_NO_STREAM_KHR) {
            /* Wait for the stream to establish connection with the producer's
             * side */
            uint32_t timeout = WL_EGL_CONN_TIMEOUT_USECS;
            EGLint state = EGL_STREAM_STATE_INITIALIZING_NV;

            do {
                usleep(WL_EGL_CONN_WAIT_USECS);
                timeout -= WL_EGL_CONN_WAIT_USECS;

                if (!data->egl.queryStream(dpy,
                                           stream,
                                           EGL_STREAM_STATE_KHR,
                                           &state)) {
                    break;
                }
            } while ((state == EGL_STREAM_STATE_INITIALIZING_NV) &&
                     (timeout > 0));

            if (state == EGL_STREAM_STATE_INITIALIZING_NV) {
                data->egl.destroyStream(dpy, stream);
                stream = EGL_NO_STREAM_KHR;
            }
        }
    }
#endif

    if (stream == EGL_NO_STREAM_KHR) {
        err = EGL_BAD_ACCESS;
        goto fail;
    }

    wlStream->eglStream = stream;
    wlStream->handle = -1;

    wlExternalApiUnlock();

    return stream;

fail:
    wlExternalApiUnlock();
    wlEglSetError(data, err);
    return EGL_NO_STREAM_KHR;
}

