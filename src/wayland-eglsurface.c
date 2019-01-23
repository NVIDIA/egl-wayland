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

#include "wayland-eglsurface.h"
#include "wayland-eglstream-client-protocol.h"
#include "wayland-eglstream-controller-client-protocol.h"
#include "wayland-eglstream-server.h"
#include "wayland-thread.h"
#include "wayland-eglutils.h"
#include "wayland-egl-ext.h"
#include <wayland-egl-backend.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <assert.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <fcntl.h>

#define WL_EGL_WINDOW_DESTROY_CALLBACK_SINCE 3

struct wl_list wlEglSurfaceList = WL_LIST_INIT(&wlEglSurfaceList);

EGLBoolean wlEglIsWlEglSurface(WlEglSurface *surface)
{
    WlEglSurface *surf;

    wl_list_for_each(surf, &wlEglSurfaceList, link) {
        if (surf == surface) {
            return EGL_TRUE;
        }
    }

    return EGL_FALSE;
}

EGLBoolean wlEglIsWaylandWindowValid(struct wl_egl_window *window)
{
    struct wl_surface *surface = NULL;

#if HAS_MINCORE
    if (!window || !wlEglPointerIsDereferencable(window)) {
        return EGL_FALSE;
    }

    surface = (struct wl_surface *)window->version;
    if (!wlEglPointerIsDereferencable(surface)) {
        surface = window->surface;
        if (!wlEglPointerIsDereferencable(surface)) {
            return EGL_FALSE;
        }
    }
#else
    /*
     * Note that dereferencing an invalid surface pointer could mean an old
     * version of libwayland-egl.so is loaded, which may not support version
     * member in wl_egl_window struct.
     */
    surface = window->surface;
#endif
    /* wl_surface is a wl_proxy, which is a wl_object. wl_objects's first
     * element points to the interface type */
    return (((*(void **)surface)) == &wl_surface_interface);
}

static void
wayland_throttleCallback(void *data,
                         struct wl_callback *callback,
                         uint32_t time)
{
    WlEglSurface *surface = (WlEglSurface *)data;
    (void) time;

    surface->throttleCallback = NULL;
    wl_callback_destroy(callback);
}

static const struct wl_callback_listener throttle_listener = {
    wayland_throttleCallback
};

void wlEglCreateFrameSync(WlEglSurface *surface, struct wl_event_queue *queue)
{
    struct wl_surface *wrapper = NULL;

    if (surface->swapInterval > 0) {
        wrapper = wl_proxy_create_wrapper(surface->wlSurface);
        wl_proxy_set_queue((struct wl_proxy *)wrapper, queue);
        surface->throttleCallback = wl_surface_frame(wrapper);
        wl_proxy_wrapper_destroy(wrapper); /* Done with wrapper */
        if (wl_callback_add_listener(surface->throttleCallback,
                                     &throttle_listener, surface) == -1) {
            return;
        }
        wl_surface_commit(surface->wlSurface);

    }
}

EGLint wlEglWaitFrameSync(WlEglSurface *surface, struct wl_event_queue *queue)
{

    WlEglDisplay *display = surface->wlEglDpy;
    int ret = 0;

    while (ret != -1 && surface->throttleCallback != NULL) {
        wlExternalApiUnlock();
        ret = wl_display_dispatch_queue(display->nativeDpy, queue);
        wlExternalApiLock();

        /* Bail out if the surface was destroyed while the lock was suspended */
        if (!wlEglIsWlEglSurface(surface)) {
            return EGL_BAD_SURFACE;
        }
    }

    if (surface->throttleCallback != NULL) {
        wl_callback_destroy(surface->throttleCallback);
        surface->throttleCallback = NULL;
        return EGL_BAD_ALLOC;
    }

    return EGL_SUCCESS;
}

EGLBoolean
wlEglSendDamageEvent(WlEglSurface *surface, struct wl_event_queue *queue)
{
    /* Attach same buffer to indicate new content for the surface is
     * made available by the client */
    wl_surface_attach(surface->wlSurface,
                      surface->ctx.wlStreamResource,
                      surface->dx,
                      surface->dy);
    wl_surface_damage(surface->wlSurface, 0, 0,
                      surface->width, surface->height);
    wl_surface_commit(surface->wlSurface);
    surface->ctx.isAttached = EGL_TRUE;
    return (wlEglRoundtrip(surface->wlEglDpy, queue) >= 0) ? EGL_TRUE :
                                                             EGL_FALSE;
}

static void*
damage_thread(void *args)
{
    WlEglSurface          *surface = (WlEglSurface*)args;
    WlEglDisplay          *display = surface->wlEglDpy;
    WlEglPlatformData     *data    = display->data;
    struct wl_event_queue *queue   = wlGetEventQueue(display);
    int                    ok      = (queue != NULL);
    EGLint                 state;

    while (ok) {
        // Unsignal sync and check latest frame and stream state
        // Done if any functions fail or stream has disconnected.
        ok = data->egl.signalSync(display->devDpy->eglDisplay,
                                  surface->ctx.damageThreadSync,
                                  EGL_UNSIGNALED_KHR)
          && data->egl.queryStreamu64(display->devDpy->eglDisplay,
                                      surface->ctx.eglStream,
                                      EGL_PRODUCER_FRAME_KHR,
                                      &surface->ctx.framesFinished)
          && data->egl.queryStream(display->devDpy->eglDisplay,
                                   surface->ctx.eglStream,
                                   EGL_STREAM_STATE_KHR,
                                   &state)
          && (state != EGL_STREAM_STATE_DISCONNECTED_KHR);

        // If flush has been requested, trigger shutdown once
        //   last produced frame has been processed.
        if (surface->ctx.damageThreadFlush) {
            if (surface->ctx.framesProcessed ==
                surface->ctx.framesProduced) {
                surface->ctx.damageThreadShutdown = 1;
            }
        }

        // If shutdown has been requested, we're done
        if (surface->ctx.damageThreadShutdown) {
            ok = 0;
        }

        // We only expect a valid wlEglWin to be set when using
        // a surface created with EGL_KHR_platform_wayland.
        if(!wlEglIsWaylandDisplay(display->nativeDpy) ||
           (surface->isSurfaceProducer && !wlEglIsWaylandWindowValid(surface->wlEglWin))) {
            ok = 0;
        }
        // If not done, keep handling frames
        if (ok) {

            // If there's an unprocessed frame ready, send damage event
            if (surface->ctx.framesFinished !=
                surface->ctx.framesProcessed) {
                if (display->exts.stream_flush) {
                    data->egl.streamFlush(display->devDpy->eglDisplay,
                                          surface->ctx.eglStream);
                }
                /* wlEglSendDamageEvent() expects the API lock to be held */
                wlExternalApiLock();
                ok = wlEglSendDamageEvent(surface, queue);
                wlExternalApiUnlock();
                surface->ctx.framesProcessed++;
             }

            // Otherwise, wait for sync to trigger
            else {
                ok = (EGL_CONDITION_SATISFIED_KHR ==
                      data->egl.clientWaitSync(display->devDpy->eglDisplay,
                                               surface->ctx.damageThreadSync,
                                               0, EGL_FOREVER_KHR));
            }
        }
    }

    data->egl.releaseThread();

    return NULL;
}

static EGLint setup_wl_eglstream_damage_thread(WlEglSurface *surface)
{
    WlEglDisplay      *display = surface->wlEglDpy;
    WlEglPlatformData *data    = display->data;
    int                ret;

    surface->ctx.damageThreadFlush    = 0;
    surface->ctx.damageThreadShutdown = 0;
    surface->ctx.framesProduced       = 0;
    surface->ctx.framesFinished       = 0;
    surface->ctx.framesProcessed      = 0;
    surface->ctx.damageThreadSync =
        data->egl.createStreamSync(display->devDpy->eglDisplay,
                                   surface->ctx.eglStream,
                                   EGL_SYNC_NEW_FRAME_NV,
                                   NULL);
    if (surface->ctx.damageThreadSync == EGL_NO_SYNC_KHR) {
        return data->egl.getError();
    }

    ret = pthread_create(&surface->ctx.damageThreadId, NULL,
                         damage_thread, (void*)surface);
    if (ret != 0) {
        return EGL_BAD_ALLOC;
    }

    return EGL_SUCCESS;
}

static void
finish_wl_eglstream_damage_thread(WlEglSurface *surface,
                                  WlEglSurfaceCtx *ctx,
                                  int immediate)
{
    WlEglDisplay      *display = surface->wlEglDpy;
    WlEglPlatformData *data    = display->data;

    if (ctx->damageThreadSync != EGL_NO_SYNC_KHR) {
        if (immediate) {
            ctx->damageThreadShutdown = 1;
        } else {
            ctx->damageThreadFlush = 1;
        }
        data->egl.signalSync(display->devDpy->eglDisplay,
                             ctx->damageThreadSync,
                             EGL_SIGNALED_KHR);
        if (ctx->damageThreadId != (pthread_t)0) {
            wlExternalApiUnlock();
            pthread_join(ctx->damageThreadId, NULL);
            wlExternalApiLock();
            ctx->damageThreadId = (pthread_t)0;
        }
        data->egl.destroySync(display->devDpy->eglDisplay,
                              ctx->damageThreadSync);
        ctx->damageThreadSync = EGL_NO_SYNC_KHR;
    }
}

static void
destroy_surface_context(WlEglSurface *surface, WlEglSurfaceCtx *ctx)
{
    WlEglDisplay      *display  = surface->wlEglDpy;
    WlEglPlatformData *data     = display->data;
    EGLDisplay         dpy      = display->devDpy->eglDisplay;
    EGLSurface         surf     = ctx->eglSurface;
    EGLStreamKHR       stream   = ctx->eglStream;
    void              *resource = ctx->wlStreamResource;

    ctx->eglSurface       = EGL_NO_SURFACE;
    ctx->eglStream        = EGL_NO_STREAM_KHR;
    ctx->wlStreamResource = NULL;

    if (surf != EGL_NO_SURFACE) {
        wlExternalApiUnlock();
        data->egl.destroySurface(dpy, surf);
        wlExternalApiLock();
    }

    if (surface->ctx.isOffscreen) {
        return;
    }

    if (surface->throttleCallback != NULL) {
        wl_callback_destroy(surface->throttleCallback);
        surface->throttleCallback = NULL;
    }

    finish_wl_eglstream_damage_thread(surface, ctx, 1);

    if (stream != EGL_NO_STREAM_KHR) {
        data->egl.destroyStream(dpy, stream);
        ctx->eglStream = EGL_NO_STREAM_KHR;
    }

    if (resource) {
        wl_buffer_destroy(resource);
    }
}

static void
discard_surface_context(WlEglSurface *surface)
{
    /* If the surface context is marked as attached, it means the compositor
     * might still be using the resources because some content was actually
     * displayed. In that case, defer its destruction until we make sure the
     * compositor doesn't need it anymore (i.e. upon stream release);
     * otherwise, we can just destroy it right away */
    if (surface->ctx.isAttached) {
        WlEglSurfaceCtx *ctx = malloc(sizeof(WlEglSurfaceCtx));
        if (ctx) {
            memcpy(ctx, &surface->ctx, sizeof(*ctx));
            wl_list_insert(&surface->oldCtxList, &ctx->link);
        }
    } else {
        destroy_surface_context(surface, &surface->ctx);
    }
}

static void
wl_buffer_release(void *data, struct wl_buffer *buffer)
{
    WlEglSurface *surface = (WlEglSurface*)data;
    WlEglSurfaceCtx *ctx;

    /* Look for the surface context for the given buffer and destroy it */
    wlExternalApiLock();
    wl_list_for_each(ctx, &surface->oldCtxList, link) {
        if (ctx->wlStreamResource == buffer) {
            destroy_surface_context(surface, ctx);
            wl_list_remove(&ctx->link);
            free(ctx);
            break;
        }
    }
    wlExternalApiUnlock();
}

static struct wl_buffer_listener wl_buffer_listener = {
    wl_buffer_release
};

static void *
create_wl_eglstream(WlEglSurface *surface,
                    int32_t handle,
                    int32_t type,
                    struct wl_array *attribs,
                    struct wl_event_queue *queue)
{
    WlEglDisplay                *display = surface->wlEglDpy;
    struct wl_egl_window        *window  = surface->wlEglWin;
    struct wl_eglstream_display *wrapper = NULL;
    struct wl_buffer            *buffer  = NULL;
    int32_t                      width;
    int32_t                      height;

    if (surface->isSurfaceProducer) {
        assert(window);
        width  = window->width;
        height = window->height;
    } else {
        width  = surface->width;
        height = surface->height;
    }

    wrapper = wl_proxy_create_wrapper(display->wlStreamDpy);
    wl_proxy_set_queue((struct wl_proxy *)wrapper, queue);

    buffer = wl_eglstream_display_create_stream(wrapper,
                                                width,
                                                height,
                                                handle,
                                                type,
                                                attribs);

    wl_proxy_wrapper_destroy(wrapper); /* Done with wrapper */

    if (!buffer) {
        return NULL;
    }

    if (wl_buffer_add_listener(buffer, &wl_buffer_listener, surface) == -1) {
        wl_buffer_destroy(buffer);
        return NULL;
    }

    return buffer;
}

static EGLint create_surface_stream_fd(WlEglSurface *surface,
                                       struct wl_event_queue *queue)
{
    WlEglDisplay         *display = surface->wlEglDpy;
    WlEglPlatformData    *data    = display->data;
    int                   handle  = EGL_NO_FILE_DESCRIPTOR_KHR;
    struct wl_array       wlAttribs;
    EGLint                eglAttribs[] = {
        EGL_STREAM_FIFO_LENGTH_KHR, surface->fifoLength,
        EGL_NONE,                   EGL_NONE,
        EGL_NONE
    };
    EGLint err = EGL_SUCCESS;

    /* We don't have any mechanism to check whether the compositor is going to
     * use this surface for composition or not when using cross_process_fd, so
     * just enable FIFO_SYNCHRONOUS if the extensions are supported */
    if (display->exts.stream_fifo_synchronous &&
        display->exts.stream_sync &&
        surface->fifoLength > 0) {
        eglAttribs[2] = EGL_STREAM_FIFO_SYNCHRONOUS_NV;
        eglAttribs[3] = EGL_TRUE;
    }

    /* First, create the EGLStream */
    surface->ctx.eglStream =
        data->egl.createStream(display->devDpy->eglDisplay, eglAttribs);
    if (surface->ctx.eglStream == EGL_NO_STREAM_KHR) {
        err = data->egl.getError();
        goto fail;
    }

    handle = data->egl.getStreamFileDescriptor(display->devDpy->eglDisplay,
                                               surface->ctx.eglStream);
    if (handle == EGL_NO_FILE_DESCRIPTOR_KHR) {
        err = data->egl.getError();
        goto fail;
    }

    /* Finally, create the wl_eglstream */
    wl_array_init(&wlAttribs); /* Empty attributes list */

    surface->ctx.wlStreamResource =
        create_wl_eglstream(surface,
                            handle,
                            WL_EGLSTREAM_HANDLE_TYPE_FD,
                            &wlAttribs,
                            queue);
    if (!surface->ctx.wlStreamResource) {
        err = EGL_BAD_ALLOC;
        goto fail;
    }

    /* Clean-up */
    close(handle);
    return EGL_SUCCESS;

fail:
    destroy_surface_context(surface, &surface->ctx);
    if (handle >= 0) {
        close(handle);
    }
    return err;
}

#ifdef EGL_NV_stream_remote
static void* acceptOneConnection(void *args)
{
    int *socket = (int *)args;
    int serverSocket = *socket;

    /* Accept only one connection and store the client socket that will be used
     * to create the EGLStream */
    *socket = accept(serverSocket, NULL, NULL);
    close(serverSocket);

    return NULL;
}

static EGLint startInetHandshake(pthread_t *thread,
                                 int *clientSocket,
                                 int *port)
{
    struct sockaddr_in addr = { 0 };
    unsigned int       addrLen;
    EGLint err = EGL_SUCCESS;
    int    ret;

    /* Create a server socket that will listen to all IPs and pick a random
     * port. Then, only one connection will be accepted. We can use clientSocket
     * to temporary store the server socket */
    *clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (*clientSocket == -1) {
        err = EGL_BAD_ALLOC;
        goto fail;
    }

    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY; /* Accept connections to all IPs */
    addr.sin_port        = htons(0);   /* Get a random port */
    if (bind(*clientSocket, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        err = EGL_BAD_ALLOC;
        goto fail;
    }
    if (listen(*clientSocket, 1) < 0) {
        err = EGL_BAD_ALLOC;
        goto fail;
    }

    /* Return host byte ordered port for sending through wayland protocol.
     * Wayland will convert back to wire format before sending. */
    addrLen = sizeof(addr);
    ret = getsockname(*clientSocket, (struct sockaddr *)&addr, &addrLen);
    if (ret != 0) {
        err = EGL_BAD_ALLOC;
        goto fail;
    }
    *port = ntohs(addr.sin_port);

    /* Start a new thread that will accept one connection only. It will store
     * the new client socket in <clientSocket>. */
    ret = pthread_create(thread, NULL, acceptOneConnection, clientSocket);
    if (ret != 0) {
        err = EGL_BAD_ALLOC;
        goto fail;
    }

    return EGL_SUCCESS;

fail:
    if (*clientSocket >= 0) {
        close(*clientSocket);
        *clientSocket = -1;
    }
    return err;
}

static EGLint finishInetHandshake(pthread_t thread, int *socket)
{
    int ret = pthread_join(thread, NULL);
    if (ret != 0 || *socket == -1) {
        return EGL_BAD_ALLOC;
    }
    return EGL_SUCCESS;
}

static EGLint create_surface_stream_remote(WlEglSurface *surface,
                                           EGLBoolean useInet,
                                           struct wl_event_queue *queue)
{
    WlEglDisplay         *display = surface->wlEglDpy;
    WlEglPlatformData    *data    = display->data;
    struct wl_array       wlAttribs;
    intptr_t             *wlAttribsData;
    EGLint                eglAttribs[] = {
        EGL_STREAM_TYPE_NV,         EGL_STREAM_CROSS_PROCESS_NV,
        EGL_STREAM_ENDPOINT_NV,     EGL_STREAM_PRODUCER_NV,
        EGL_STREAM_PROTOCOL_NV,     EGL_STREAM_PROTOCOL_SOCKET_NV,
        EGL_SOCKET_TYPE_NV,         EGL_DONT_CARE,
        EGL_SOCKET_HANDLE_NV,       -1,
        EGL_NONE,                   EGL_NONE,
        EGL_NONE,
    };
    pthread_t  thread;
    int        socket[2];
    int        port;
    EGLint     err     = EGL_SUCCESS;
    int        ret;

    wl_array_init(&wlAttribs); /* Empty attributes list */

    if (useInet) {
        /* Start inet handshaking and get the socket and selected port */
        err = startInetHandshake(&thread, &socket[0], &port);
        if (err != EGL_SUCCESS) {
            goto fail;
        }

        /* Fill the wlAttribs array with the connection data. */
        if (!wl_array_add(&wlAttribs, 4*sizeof(intptr_t))) {
            err = EGL_BAD_ALLOC;
            goto fail;
        }

        /* Get host byte ordered address for sending through wayland protocol.
         * Wayland will convert back to wire format before sending. Assume a
         * local INET connection until cross partition wayland support is added.
         */
        wlAttribsData = (intptr_t *)wlAttribs.data;
        wlAttribsData[0] = WL_EGLSTREAM_ATTRIB_INET_ADDR;
        wlAttribsData[1] = (intptr_t)INADDR_LOOPBACK;
        wlAttribsData[2] = WL_EGLSTREAM_ATTRIB_INET_PORT;
        wlAttribsData[3] = (intptr_t)port;

        if (wlAttribsData[1] == -1) {
           err = EGL_BAD_ALLOC;
           goto fail;
        }

        /* Create a dummy fd to be feed into wayland. The fd will never be used,
         * but wayland will abort if an invaild fd is given.
         */
        socket[1] = open("/dev/null", O_RDONLY);
        if (socket[1] == -1) {
           err = EGL_BAD_ALLOC;
           goto fail;
        }
    } else {
        /* Create a new socket pair for both EGLStream endpoints */
        ret = socketpair(AF_UNIX, SOCK_STREAM, 0, socket);
        if (ret != 0) {
            err = EGL_BAD_ALLOC;
            goto fail;
        }
    }

    surface->ctx.wlStreamResource =
        create_wl_eglstream(surface,
                            socket[1],
                            (useInet ? WL_EGLSTREAM_HANDLE_TYPE_INET :
                                       WL_EGLSTREAM_HANDLE_TYPE_SOCKET),
                            &wlAttribs,
                            queue);
    if (!surface->ctx.wlStreamResource) {
        err = EGL_BAD_ALLOC;
        goto fail;
    }

    /* Need a roundtrip for the consumer's endpoint to be created before the
     * producer's */
    if (wlEglRoundtrip(display, queue) < 0) {
        err = EGL_BAD_ALLOC;
        goto fail;
    }

    if (useInet) {
        /* Wait for the inet handshake to finish */
        err = finishInetHandshake(thread, &socket[0]);
        if (err != EGL_SUCCESS) {
            goto fail;
        }
    }

    /* Finally, create the EGLStream
     *
     *   EGL_SOCKET_TYPE_NV   is in eglAttribs[6]
     *   EGL_SOCKET_HANDLE_NV is in eglAttribs[8]
     */
    eglAttribs[7] = (useInet ? EGL_SOCKET_TYPE_INET_NV :
                               EGL_SOCKET_TYPE_UNIX_NV);
    eglAttribs[9] = socket[0];

    if (!surface->isSurfaceProducer &&
        display->wlStreamCtlVer >= WL_EGLSTREAM_CONTROLLER_ATTACH_EGLSTREAM_CONSUMER_ATTRIB_SINCE) {
        eglAttribs[10] = EGL_STREAM_FIFO_LENGTH_KHR;
        eglAttribs[11] = surface->fifoLength;
    }

    surface->ctx.eglStream =
        data->egl.createStream(display->devDpy->eglDisplay, eglAttribs);
    if (surface->ctx.eglStream == EGL_NO_STREAM_KHR) {
       err = data->egl.getError();
       goto fail;
    }

    /* Close server socket on the client side */
    if (socket[1] >= 0) {
        close(socket[1]);
    }

    return EGL_SUCCESS;

fail:
    destroy_surface_context(surface, &surface->ctx);
    wl_array_release(&wlAttribs);
    if (socket[0] >= 0) {
        close(socket[0]);
    }
    if (!useInet && socket[1] >= 0) {
        close(socket[1]);
    }
    return err;
}
#endif

static EGLint
create_surface_stream(WlEglSurface *surface, struct wl_event_queue *queue)
{
    WlEglDisplay *display = surface->wlEglDpy;
    EGLint        err     = EGL_BAD_ACCESS;

    /* Try all supported EGLStream creation methods until one of them succeeds.
     * More efficient connection schemes should be given a higher priority. If
     * there is more than one method giving the same efficiency, the more
     * versatile/configurable one would be preferred:
     *
     *    1. Cross-process unix sockets
     *    2. Cross-process FD
     *    3. Cross-process inet sockets
     */
#ifdef EGL_NV_stream_remote
    if ((err != EGL_SUCCESS) &&
        display->caps.stream_socket &&
        display->exts.stream_remote) {
        err = create_surface_stream_remote(surface, EGL_FALSE, queue);
    }
#endif

    if ((err != EGL_SUCCESS) &&
        display->caps.stream_fd &&
        display->exts.stream_cross_process_fd) {
        err = create_surface_stream_fd(surface, queue);
    }

#ifdef EGL_NV_stream_remote
    if ((err != EGL_SUCCESS) &&
        display->caps.stream_inet &&
        display->exts.stream_remote) {
        err = create_surface_stream_remote(surface, EGL_TRUE, queue);
    }
#endif

    return err;
}

static EGLint
create_surface_context(WlEglSurface *surface)
{
    WlEglDisplay          *display     = surface->wlEglDpy;
    WlEglPlatformData     *data        = display->data;
    struct wl_egl_window  *window      = surface->wlEglWin;
    struct wl_event_queue *queue       = NULL;
    EGLint                 synchronous = EGL_FALSE;
    EGLint                 err         = EGL_SUCCESS;
    struct wl_array        wlAttribs;
    intptr_t              *wlAttribsData;

    assert(surface->ctx.eglSurface == EGL_NO_SURFACE);

    queue = wlGetEventQueue(display);
    if (!queue) {
        err = EGL_BAD_ALLOC;
        goto fail;
    }

    /* Width and height are the first and second attributes respectively */
    if (surface->isSurfaceProducer) {
        surface->attribs[1] = window->width;
        surface->attribs[3] = window->height;
    }

    /* First, create the underlying wl_eglstream and EGLStream */
    err = create_surface_stream(surface, queue);
    if (err != EGL_SUCCESS) {
        goto fail;
    }

    /* Then attach the wl_eglstream so the compositor connects a consumer to the
     * EGLStream */
    if (display->wlStreamCtl != NULL) {
        if (display->wlStreamCtlVer >=
            WL_EGLSTREAM_CONTROLLER_ATTACH_EGLSTREAM_CONSUMER_ATTRIB_SINCE) {
            wl_array_init(&wlAttribs);

            if (!wl_array_add(&wlAttribs, 2 * sizeof(intptr_t))) {
                wl_array_release(&wlAttribs);
                err = EGL_BAD_ALLOC;
                goto fail;
            }

            wlAttribsData = (intptr_t *)wlAttribs.data;
            wlAttribsData[0] = WL_EGLSTREAM_CONTROLLER_ATTRIB_PRESENT_MODE;

            if (surface->isSurfaceProducer) {
                wlAttribsData[1] = WL_EGLSTREAM_CONTROLLER_PRESENT_MODE_DONT_CARE;
            } else if (surface->fifoLength > 0) {
                if (!wl_array_add(&wlAttribs, 2 * sizeof(intptr_t))) {
                    wl_array_release(&wlAttribs);
                    err = EGL_BAD_ALLOC;
                    goto fail;
                }
                wlAttribsData    = (intptr_t *)wlAttribs.data;
                wlAttribsData[1] = WL_EGLSTREAM_CONTROLLER_PRESENT_MODE_FIFO;
                wlAttribsData[2] = WL_EGLSTREAM_CONTROLLER_ATTRIB_FIFO_LENGTH;
                wlAttribsData[3] = surface->fifoLength;
            } else {
                wlAttribsData[1] = WL_EGLSTREAM_CONTROLLER_PRESENT_MODE_MAILBOX;
            }

            wl_eglstream_controller_attach_eglstream_consumer_attribs(display->wlStreamCtl,
                                                                      surface->wlSurface,
                                                                      surface->ctx.wlStreamResource,
                                                                      &wlAttribs);
        } else {
            wl_eglstream_controller_attach_eglstream_consumer(display->wlStreamCtl,
                                                              surface->wlSurface,
                                                              surface->ctx.wlStreamResource);
        }
    } else {
        wl_surface_attach(surface->wlSurface,
                          surface->ctx.wlStreamResource,
                          window->dx,
                          window->dy);
        wl_surface_commit(surface->wlSurface);

        /* Since we are using the legacy method of overloading wl_surface_attach
         * in order to create the server-side EGLStream here, the compositor
         * will actually take this as a new buffer. We mark it as 'attached'
         * because whenever a new wl_surface_attach request is issued, the
         * compositor will emit back a wl_buffer_release event, and we will
         * destroy the context then. */
        surface->ctx.isAttached = EGL_TRUE;
    }

    if (wlEglRoundtrip(display, queue) < 0) {
        err = EGL_BAD_ALLOC;
        goto fail;
    }

    if (surface->isSurfaceProducer) {
        /* Finally, create the surface producer */
        surface->ctx.eglSurface =
            data->egl.createStreamProducerSurface(display->devDpy->eglDisplay,
                                                  surface->eglConfig,
                                                  surface->ctx.eglStream,
                                                  surface->attribs);
        if (surface->ctx.eglSurface == EGL_NO_SURFACE) {
            err = data->egl.getError();
            goto fail;
        }
        wl_display_flush(display->nativeDpy);
    }

    /* Check whether we should use a damage thread */
    surface->ctx.useDamageThread =
                    display->exts.stream_fifo_synchronous &&
                    display->exts.stream_sync &&
                    data->egl.queryStream(display->devDpy->eglDisplay,
                                          surface->ctx.eglStream,
                                          EGL_STREAM_FIFO_SYNCHRONOUS_NV,
                                          &synchronous) &&
                    (synchronous == EGL_TRUE);
    if (surface->ctx.useDamageThread) {
        err = setup_wl_eglstream_damage_thread(surface);
        if (err != EGL_SUCCESS) {
            goto fail;
        }
    }

    /* Cache current window size and displacement for future checks */
    if (surface->isSurfaceProducer) {
        surface->width = window->width;
        surface->height = window->height;
        surface->dx = window->dx;
        surface->dy = window->dy;
        window->attached_width = surface->width;
        window->attached_height = surface->height;
    }

    return EGL_SUCCESS;

fail:
    destroy_surface_context(surface, &surface->ctx);
    return err;
}

EGLBoolean wlEglInitializeSurfaceExport(WlEglSurface *surface)
{
    WlEglDisplay *display = surface->wlEglDpy;

    wlExternalApiLock();
    wl_list_init(&surface->oldCtxList);
    wl_list_insert(&wlEglSurfaceList, &surface->link);
    if (create_surface_context(surface) != EGL_SUCCESS) {
        wlExternalApiUnlock();
        return EGL_FALSE;
    }

    wl_eglstream_display_swap_interval(display->wlStreamDpy,
                                       surface->ctx.wlStreamResource,
                                       surface->swapInterval);

    wlExternalApiUnlock();
    return EGL_TRUE;
}

static void
resize_callback(struct wl_egl_window *window, void *data)
{
    WlEglDisplay      *display = NULL;
    WlEglPlatformData *pData   = NULL;
    WlEglSurface      *surface = (WlEglSurface *)data;
    EGLint             err     = EGL_SUCCESS;

    if (!window || !surface) {
        return;
    }

    display = surface->wlEglDpy;
    if (!wlEglIsWaylandDisplay(display->nativeDpy) ||
        !wlEglIsWaylandWindowValid(surface->wlEglWin)) {
        return;
    }
    pData = display->data;

    wlExternalApiLock();

    /* Resize stream only if window geometry has changed */
    if ((surface->width != window->width) ||
        (surface->height != window->height) ||
        (surface->dx != window->dx) ||
        (surface->dy != window->dy)) {
        // If a damage thread is in use, wait for it to finish processing all
        //   pending frames
        finish_wl_eglstream_damage_thread(surface, &surface->ctx, 0);

        discard_surface_context(surface);
        surface->ctx.wlStreamResource = NULL;
        surface->ctx.isAttached = EGL_FALSE;
        surface->ctx.eglSurface = EGL_NO_SURFACE;
        surface->ctx.eglStream = EGL_NO_STREAM_KHR;
        surface->ctx.damageThreadSync = EGL_NO_SYNC_KHR;
        surface->ctx.damageThreadId = (pthread_t)0;

        err = create_surface_context(surface);
        if (err == EGL_SUCCESS) {
            /* We are handing execution control over to EGL here, passing
             * external objects down. Thus, it will re-enter the external
             * platform in order to resolve such objects to their internal
             * representations.
             *
             * XXX: Note that we are using external objects here. If another
             *      thread destroys them while we are still making the surface
             *      current, they will become invalid. This should be resolved
             *      once we refactor our EGL API wrappers, removing the need
             *      for resolving external objects into their internal
             *      representations.
             */
            wlExternalApiUnlock();
            pData->egl.makeCurrent(display,
                                   surface,
                                   surface,
                                   pData->egl.getCurrentContext());
            wlExternalApiLock();

            /* Reset swap interval */
            wl_eglstream_display_swap_interval(display->wlStreamDpy,
                                               surface->ctx.wlStreamResource,
                                               surface->swapInterval);
        }
    }

    wlExternalApiUnlock();
}

static EGLBoolean validateSurfaceAttrib(EGLAttrib attrib, EGLAttrib value)
{
    switch (attrib) {
    /* Window-only attributes will be ignored, but we still need to make sure a
     * valid value is given */
    case EGL_RENDER_BUFFER:
        return (value == EGL_BACK_BUFFER) ? EGL_TRUE :
                                            EGL_FALSE;
    case EGL_POST_SUB_BUFFER_SUPPORTED_NV:
        return (value == EGL_TRUE ||
                value == EGL_FALSE) ? EGL_TRUE :
                                      EGL_FALSE;

    /* EGL_WIDTH and EGL_HEIGHT shouldn't be specified */
    case EGL_WIDTH:
    case EGL_HEIGHT:
        return EGL_FALSE;

    /* If attribute is supported/unsupported for both EGL_WINDOW_BIT and
     * EGL_STREAM_BIT_KHR, then that will be handled inside the actual
     * eglCreateStreamProducerSurfaceKHR() */
    default:
        return EGL_TRUE;
    }
}

static EGLint assignWlEglSurfaceAttribs(WlEglSurface *surface,
                                        const EGLAttrib *attribs)
{
    EGLint *int_attribs = NULL;
    unsigned int nAttribs = 2; // At least width and height
    int i;

    if (attribs) {
        for (i = 0; attribs[i] != EGL_NONE; i += 2) {
            if (!validateSurfaceAttrib(attribs[i], attribs[i + 1])) {
                return EGL_BAD_ATTRIBUTE;
            }

            /* Filter out window-only attributes */
            if ((attribs[i] != EGL_RENDER_BUFFER) &&
                (attribs[i] != EGL_POST_SUB_BUFFER_SUPPORTED_NV)) {
                nAttribs++;
            }
        }
    }

    int_attribs = (EGLint *)malloc(((nAttribs * 2) + 1) * sizeof(*int_attribs));
    if (!int_attribs) {
        return EGL_BAD_ALLOC;
    }

    nAttribs = 0;

    int_attribs[nAttribs++] = EGL_WIDTH; // width at offset 0
    int_attribs[nAttribs++] = 0;
    int_attribs[nAttribs++] = EGL_HEIGHT; // height at offset 2
    int_attribs[nAttribs++] = 0;

    if (attribs) {
        for (i = 0; attribs[i] != EGL_NONE; i += 2) {
            if ((attribs[i] != EGL_RENDER_BUFFER) &&
                (attribs[i] != EGL_POST_SUB_BUFFER_SUPPORTED_NV)) {
                int_attribs[nAttribs++] = (EGLint)attribs[i];
                int_attribs[nAttribs++] = (EGLint)attribs[i + 1];
            }
        }
    }

    int_attribs[nAttribs] = EGL_NONE;

    surface->attribs = int_attribs;

    return EGL_SUCCESS;
}

static EGLint destroyEglSurface(EGLDisplay dpy, EGLSurface eglSurface)
{
    WlEglDisplay          *display = (WlEglDisplay*)dpy;
    WlEglSurface          *surface = (WlEglSurface*)eglSurface;
    struct wl_event_queue *queue   = NULL;
    WlEglSurfaceCtx       *ctx     = NULL;
    WlEglSurfaceCtx       *next    = NULL;
    int                    ret     = 0;

    if (!wlEglIsWlEglSurface(surface)) {
        return EGL_BAD_SURFACE;
    }

    /* Remove surface from wlEglSurfaceList as early as possible to avoid
     * multiple threads trying to destroy the same surface simultaneously.
     * Inside destroy_surface_context and finish_wl_eglstream_damage_thread,
     * wl external API lock could be released temporarily, which would allow
     * multiple threads grab the same surface from wlEglSurfaceList, enter
     * destroyEglSurface and lead to segmentation fault. */
    wl_list_remove(&surface->link);

    if (!surface->ctx.isOffscreen) {
        // Force damage thread to exit before invalidating the window objects
        finish_wl_eglstream_damage_thread(surface, &surface->ctx, 1);

        // We only expect a valid wlEglWin to be set when using
        // a surface created with EGL_KHR_platform_wayland.
        if (wlEglIsWaylandDisplay(display->nativeDpy) &&
           (wlEglIsWaylandWindowValid(surface->wlEglWin) ||
           (!surface->isSurfaceProducer))) {
            queue = wlGetEventQueue(display);
            if (queue != NULL) {
                wl_surface_attach(surface->wlSurface, NULL, 0, 0);
                wl_surface_commit(surface->wlSurface);
                ret = wlEglRoundtrip(display, queue);
            }

            if (surface->isSurfaceProducer) {
                surface->wlEglWin->driver_private = NULL;
                surface->wlEglWin->resize_callback = NULL;
                if (surface->wlEglWinVer >= WL_EGL_WINDOW_DESTROY_CALLBACK_SINCE) {
                    surface->wlEglWin->destroy_window_callback = NULL;
                }
            }
        }

        wl_list_for_each_safe(ctx, next, &surface->oldCtxList, link) {
            destroy_surface_context(surface, ctx);
            wl_list_remove(&ctx->link);
            free(ctx);
        }

        free(surface->attribs);
    }

    destroy_surface_context(surface, &surface->ctx);

    free(surface);

    return (ret >= 0) ? EGL_SUCCESS : EGL_BAD_DISPLAY;
}

static void
destroy_callback(void *data)
{
    WlEglSurface *surface = (WlEglSurface*)data;

    if (!surface || !wlEglIsWlEglDisplay(surface->wlEglDpy)) {
        return;
    }

    wlExternalApiLock();
    destroyEglSurface((EGLDisplay)surface->wlEglDpy,
                      (EGLSurface)surface);
    wlExternalApiUnlock();
}

static void
getWlEglWindowVersionAndSurface(struct wl_egl_window *window,
                                long int             *version,
                                struct wl_surface   **surface)
{
    /*
     * Given that wl_egl_window wasn't always a versioned struct, and that
     * 'window->version' replaced 'window->surface', we must check whether
     * 'window->version' is actually a valid pointer. If it is, we are dealing
     * with a wl_egl_window from an old implementation of libwayland-egl.so
     * Note that this will be disabled if platfrom does not have
     * mincore(2) to check whether a pointer is valid or not.
     */

     *version = window->version;
     *surface = window->surface;

#if HAS_MINCORE
    if (wlEglPointerIsDereferencable((void *)(window->version))) {
        *version = 0;
        *surface = (struct wl_surface *)(window->version);
    }
#endif

}

EGLSurface wlEglCreatePlatformWindowSurfaceHook(EGLDisplay dpy,
                                                EGLConfig config,
                                                void *nativeWin,
                                                const EGLAttrib *attribs)
{
    WlEglDisplay         *display = (WlEglDisplay*)dpy;
    WlEglPlatformData    *data    = display->data;
    WlEglSurface         *surface = NULL;
    struct wl_egl_window *window  = (struct wl_egl_window *)nativeWin;
    EGLBoolean            res     = EGL_FALSE;
    EGLint                err     = EGL_SUCCESS;
    EGLint                surfType;

    wlExternalApiLock();

    if (!wlEglIsWaylandDisplay(display->nativeDpy)) {
        err = EGL_BAD_DISPLAY;
        goto fail;
    }

    dpy = display->devDpy->eglDisplay;

    if (!wlEglIsWaylandWindowValid(window)) {
        err = EGL_BAD_NATIVE_WINDOW;
        goto fail;
    }

    // Check for existing associated surface
    if (window->driver_private != NULL) {
        err = EGL_BAD_ALLOC;
        goto fail;
    }

    wlExternalApiUnlock();
    res = data->egl.getConfigAttrib(dpy, config, EGL_SURFACE_TYPE, &surfType);
    wlExternalApiLock();

    if (!res || !(surfType & EGL_STREAM_BIT_KHR)) {
        err = EGL_BAD_CONFIG;
        goto fail;
    }

    if (!display->exts.stream ||
        (!display->exts.stream_cross_process_fd &&
         !display->exts.stream_remote) ||
        !display->exts.stream_producer_eglsurface) {
        err = EGL_BAD_ALLOC;
        goto fail;
    }

    surface = calloc(1, sizeof(*surface));
    if (!surface) {
        err = EGL_BAD_ALLOC;
        goto fail;
    }

    surface->wlEglDpy = display;
    surface->eglConfig = config;
    surface->wlEglWin = window;
    surface->ctx.eglStream = EGL_NO_STREAM_KHR;
    surface->ctx.eglSurface = EGL_NO_SURFACE;
    surface->ctx.isOffscreen = EGL_FALSE;
    surface->isSurfaceProducer = EGL_TRUE;
    // FIFO_LENGTH == 1 to set FIFO mode, FIFO_LENGTH == 0 to set MAILBOX mode
    surface->fifoLength = (display->exts.stream_fifo_synchronous &&
                           display->exts.stream_sync) ? 1 : 0;

    getWlEglWindowVersionAndSurface(window,
                                    &surface->wlEglWinVer,
                                    &surface->wlSurface);
    wl_list_init(&surface->oldCtxList);
    wl_list_insert(&wlEglSurfaceList, &surface->link);

    err = assignWlEglSurfaceAttribs(surface, attribs);
    if (err != EGL_SUCCESS) {
        goto fail;
    }

    err = create_surface_context(surface);
    if (err != EGL_SUCCESS) {
        goto fail;
    }

    surface->swapInterval = 1; // Default swap interval is 1

    // Set client's swap interval if there is an override on the compositor
    // side, Otherwise set to default
    wl_eglstream_display_swap_interval(display->wlStreamDpy,
                                       surface->ctx.wlStreamResource,
                                       surface->swapInterval);
    window->driver_private = surface;
    window->resize_callback = resize_callback;
    if (surface->wlEglWinVer >= WL_EGL_WINDOW_DESTROY_CALLBACK_SINCE) {
        window->destroy_window_callback = destroy_callback;
    }
    wlExternalApiUnlock();

    return surface;

fail:
    if (surface) {
        destroyEglSurface(display, surface);
    }

    wlExternalApiUnlock();

    wlEglSetError(data, err);

    return EGL_NO_SURFACE;
}

EGLSurface wlEglCreatePlatformPixmapSurfaceHook(EGLDisplay dpy,
                                                EGLConfig config,
                                                void *nativePixmap,
                                                const EGLAttrib *attribs)
{
    WlEglDisplay *display = (WlEglDisplay*)dpy;
    (void) config;
    (void) nativePixmap;
    (void) attribs;

    /* Wayland does not support pixmap types. See EGL_EXT_platform_wayland. */
    wlEglSetError(display->data, EGL_BAD_PARAMETER);
    return EGL_NO_SURFACE;
}

EGLSurface wlEglCreatePbufferSurfaceHook(EGLDisplay dpy,
                                         EGLConfig config,
                                         const EGLint *attribs)
{
    WlEglDisplay      *display = (WlEglDisplay*)dpy;
    WlEglPlatformData *data    = display->data;
    WlEglSurface      *surface = NULL;
    EGLSurface         surf    = EGL_NO_SURFACE;
    EGLint             err     = EGL_SUCCESS;

    /* Nothing really special needs to be done. Just fall back to the driver's
     * Pbuffer surface creation function */
    dpy = display->devDpy->eglDisplay;
    surf = data->egl.createPbufferSurface(dpy, config, attribs);

    if (surf == EGL_NO_SURFACE) {
        goto fail;
    }

    surface = calloc(1, sizeof(*surface));
    if (!surface) {
        err = EGL_BAD_ALLOC;
        goto fail;
    }

    surface->wlEglDpy = display;
    surface->eglConfig = config;
    surface->ctx.eglSurface = surf;
    surface->ctx.isOffscreen = EGL_TRUE;
    wl_list_init(&surface->oldCtxList);

    wlExternalApiLock();
    wl_list_insert(&wlEglSurfaceList, &surface->link);
    wlExternalApiUnlock();

    return surface;

fail:
    if (err != EGL_SUCCESS) {
        wlEglSetError(data, err);
    }
    return EGL_NO_SURFACE;
}

EGLSurface wlEglCreateStreamProducerSurfaceHook(EGLDisplay dpy,
                                                EGLConfig config,
                                                EGLStreamKHR stream,
                                                const EGLint *attribs)
{
    WlEglDisplay      *display = (WlEglDisplay*)dpy;
    WlEglPlatformData *data    = display->data;
    WlEglSurface      *surface = NULL;
    EGLSurface         surf    = EGL_NO_SURFACE;
    EGLint             err     = EGL_SUCCESS;

    /* Nothing really special needs to be done. Just fall back to the driver's
     * stream producer surface creation function */
    dpy = display->devDpy->eglDisplay;
    surf = data->egl.createStreamProducerSurface(dpy, config, stream, attribs);

    if (surf == EGL_NO_SURFACE) {
        goto fail;
    }

    surface = calloc(1, sizeof(*surface));
    if (!surface) {
        err = EGL_BAD_ALLOC;
        goto fail;
    }

    surface->wlEglDpy = display;
    surface->eglConfig = config;
    surface->ctx.eglSurface = surf;
    surface->ctx.isOffscreen = EGL_TRUE;
    wl_list_init(&surface->oldCtxList);

    wlExternalApiLock();
    wl_list_insert(&wlEglSurfaceList, &surface->link);
    wlExternalApiUnlock();

    return surface;

fail:
    if (err != EGL_SUCCESS) {
        wlEglSetError(data, err);
    }
    return EGL_NO_SURFACE;
}

EGLBoolean wlEglDestroySurfaceHook(EGLDisplay dpy, EGLSurface eglSurface)
{
    WlEglDisplay *display = (WlEglDisplay*)dpy;
    EGLBoolean    err     = EGL_SUCCESS;

    if (!wlEglIsWlEglDisplay(display)) {
        wlEglSetError(display->data, EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }

    wlExternalApiLock();
    err = destroyEglSurface(dpy, eglSurface);
    wlExternalApiUnlock();

    if (err != EGL_SUCCESS) {
        wlEglSetError(display->data, err);
        return EGL_FALSE;
    }

    return EGL_TRUE;
}

EGLBoolean wlEglDestroyAllSurfaces(WlEglDisplay *display)
{
    WlEglSurface *surface, *next;
    EGLBoolean res = EGL_TRUE;

    wl_list_for_each_safe(surface, next, &wlEglSurfaceList, link) {
        if (surface->wlEglDpy == display) {
            res = (destroyEglSurface(display, surface) == EGL_SUCCESS) && res;
        }
    }

    return res;
}

EGLBoolean wlEglQueryNativeResourceHook(EGLDisplay dpy,
                                        void *nativeResource,
                                        EGLint attribute,
                                        int *value)
{
    struct wl_eglstream_display *wlStreamDpy = NULL;
    struct wl_eglstream         *wlStream    = NULL;
    EGLBoolean                   res         = EGL_FALSE;

    wlExternalApiLock();

    wlStreamDpy = wl_eglstream_display_get(dpy);
    if (!wlStreamDpy) {
        goto done;
    }

    wlStream = wl_eglstream_display_get_stream(
                                        wlStreamDpy,
                                        (struct wl_resource *)nativeResource);
    if(!wlStream) {
        goto done;
    }

    switch (attribute) {
    case EGL_WIDTH:
        *value = (int)wlStream->width;
        res = EGL_TRUE;
        goto done;
    case EGL_HEIGHT:
        *value = (int)wlStream->height;
        res = EGL_TRUE;
        goto done;
    case EGL_WAYLAND_Y_INVERTED_WL:
        /* GLTexture consumers are the only mechanism currently used to map
         * buffers composited by the wayland compositor. They define the buffer
         * origin as the lower left corner, which matches to what the wayland
         * compositor would consider as non-y-inverted */
        *value = (int)EGL_FALSE;
        res = EGL_TRUE;
        goto done;
    }

done:
    wlExternalApiUnlock();
    return res;
}
