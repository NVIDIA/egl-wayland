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

#include "wayland-eglsurface.h"
#include "wayland-eglstream-client-protocol.h"
#include "wayland-eglstream-server.h"
#include "wayland-api-lock.h"
#include "wayland-eglutils.h"
#include "wayland-egl-priv.h"
#include "wayland-egl-ext.h"
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <assert.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>

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
    if (!window || !wlEglPointerIsDereferencable(window)) {
        return EGL_FALSE;
    }

    if (!window->surface || !wlEglPointerIsDereferencable(window->surface)) {
        return EGL_FALSE;
    }

    /* wl_surface is a wl_proxy, which is a wl_object. wl_objects's first
     * element points to the interface type */
    return (((*(void **)window->surface)) == &wl_surface_interface);
}

static void
wayland_throttleCallback(void *data,
                          struct wl_callback *callback,
                          uint32_t time)
{
    WlEglSurface *surface = (WlEglSurface *)data;

    surface->throttleCallback = NULL;
    wl_callback_destroy(callback);
};

static const struct wl_callback_listener throttle_listener = {
    wayland_throttleCallback
};

void wlEglCreateFrameSync(WlEglSurface *surface, struct wl_event_queue *queue)
{
    if (surface->swapInterval > 0) {
        surface->throttleCallback = wl_surface_frame(surface->wlEglWin->surface);
        wl_callback_add_listener(surface->throttleCallback,
                                 &throttle_listener, surface);
        wl_proxy_set_queue((struct wl_proxy *) surface->throttleCallback,
                            queue);
        wl_surface_commit(surface->wlEglWin->surface);

    }
}

EGLint wlEglWaitFrameSync(WlEglSurface *surface, struct wl_event_queue *queue)
{

    WlEglDisplay *display = surface->wlEglDpy;

    wlExternalApiUnlock();
    while (surface->throttleCallback != NULL) {
        if (wl_display_dispatch_queue(display->nativeDpy,
                                      queue) == -1) {
            break;
        }
    }
    wlExternalApiLock();

    if (surface->throttleCallback != NULL) {
        wl_callback_destroy(surface->throttleCallback);
        surface->throttleCallback = NULL;
        return -1;
    }
    return 1;
}

EGLBoolean
wlEglSendDamageEvent(WlEglSurface *surface)
{
    /* Attach same buffer to indicate new content for the surface is
     * made available by the client */
    wl_surface_attach(surface->wlEglWin->surface,
                      surface->ctx.wlStreamResource,
                      surface->dx,
                      surface->dy);
    wl_surface_damage(surface->wlEglWin->surface, 0, 0,
                      surface->width, surface->height);
    wl_surface_commit(surface->wlEglWin->surface);
    return (wlEglRoundtrip(surface->wlEglDpy,
            surface->wlEglDpy->wlDamageEventQueue) >= 0) ? EGL_TRUE : EGL_FALSE;
}

static void*
damage_thread(void *args)
{
    WlEglSurface      *surface = (WlEglSurface*)args;
    WlEglDisplay      *display = surface->wlEglDpy;
    WlEglPlatformData *data    = display->data;
    int                ok      = 1;
    EGLint             state;

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

        if(!wlEglIsWaylandDisplay(display->nativeDpy) ||
           !wlEglIsWaylandWindowValid(surface->wlEglWin)) {
            ok = 0;
        }
        // If not done, keep handling frames
        if (ok) {

            // If there's an unprocessed frame ready, send damage event
            if (surface->ctx.framesFinished !=
                surface->ctx.framesProcessed) {
                /* wlEglSendDamageEvent() expects the API lock to be held */
                wlExternalApiLock();
                ok = wlEglSendDamageEvent(surface);
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

    finish_wl_eglstream_damage_thread(surface, ctx, 1);

    if (stream != EGL_NO_STREAM_KHR) {
        data->egl.destroyStream(dpy, stream);
    }

    if (resource) {
        wl_buffer_destroy(resource);
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

static EGLint create_surface_stream_fd(WlEglSurface *surface)
{
    WlEglDisplay         *display = surface->wlEglDpy;
    WlEglPlatformData    *data    = display->data;
    struct wl_egl_window *window  = surface->wlEglWin;
    int                   handle  = EGL_NO_FILE_DESCRIPTOR_KHR;
    struct wl_array       wlAttribs;
    EGLint                eglAttribs[] = {
        EGL_STREAM_FIFO_LENGTH_KHR, 1,
        EGL_NONE,                   EGL_NONE,
        EGL_NONE
    };
    EGLint err = EGL_SUCCESS;

    /* We don't have any mechanism to check whether the compositor is going to
     * use this surface for composition or not when using cross_process_fd, so
     * just enable FIFO_SYNCHRONOUS if the extensions are supported */
    if (display->exts.stream_fifo_synchronous &&
        display->exts.stream_sync) {
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
        wl_eglstream_display_create_stream(display->wlStreamDpy,
                                           window->width,
                                           window->height,
                                           handle,
                                           WL_EGLSTREAM_HANDLE_TYPE_FD,
                                           &wlAttribs);
    if (!surface->ctx.wlStreamResource) {
        err = EGL_BAD_ALLOC;
        goto fail;
    }

    wl_proxy_set_queue((struct wl_proxy *)surface->ctx.wlStreamResource,
                        display->wlQueue);
    wl_buffer_add_listener(surface->ctx.wlStreamResource,
                           &wl_buffer_listener,
                           surface);

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
    struct sockaddr_in addr;
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
    getsockname(*clientSocket, (struct sockaddr *)&addr, &addrLen);
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
                                           EGLBoolean useInet)
{
    WlEglDisplay         *display = surface->wlEglDpy;
    WlEglPlatformData    *data    = display->data;
    struct wl_egl_window *window  = surface->wlEglWin;
    struct wl_array       wlAttribs;
    intptr_t             *wlAttribsData;
    EGLint                eglAttribs[] = {
        EGL_STREAM_TYPE_NV,         EGL_STREAM_CROSS_PROCESS_NV,
        EGL_STREAM_ENDPOINT_NV,     EGL_STREAM_PRODUCER_NV,
        EGL_STREAM_PROTOCOL_NV,     EGL_STREAM_PROTOCOL_SOCKET_NV,
        EGL_SOCKET_TYPE_NV,         EGL_DONT_CARE,
        EGL_SOCKET_HANDLE_NV,       -1,
        EGL_NONE
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
         * local INET connection until cross partition wayland suppor is added.
         */
        wlAttribsData = (intptr_t *)wlAttribs.data;
        wlAttribsData[0] = WL_EGLSTREAM_ATTRIB_INET_ADDR;
        wlAttribsData[1] = (intptr_t)inet_network("127.0.0.1");
        wlAttribsData[2] = WL_EGLSTREAM_ATTRIB_INET_PORT;
        wlAttribsData[3] = (intptr_t)port;

        if (wlAttribsData[1] == -1) {
           err = EGL_BAD_ALLOC;
           goto fail;
        }

        socket[1] = 0; /* unused */
    } else {
        /* Create a new socket pair for both EGLStream endpoints */
        ret = socketpair(AF_UNIX, SOCK_STREAM, 0, socket);
        if (ret != 0) {
            err = EGL_BAD_ALLOC;
            goto fail;
        }
    }

    /* Create the wl_eglstream */
    surface->ctx.wlStreamResource =
        wl_eglstream_display_create_stream(
                                    display->wlStreamDpy,
                                    window->width,
                                    window->height,
                                    socket[1],
                                    (useInet ? WL_EGLSTREAM_HANDLE_TYPE_INET :
                                               WL_EGLSTREAM_HANDLE_TYPE_SOCKET),
                                    &wlAttribs);
    if (!surface->ctx.wlStreamResource) {
        err = EGL_BAD_ALLOC;
        goto fail;
    }

    /* Setup the wl_eglstream events queue and callbacks */
    wl_proxy_set_queue((struct wl_proxy *)surface->ctx.wlStreamResource,
                       display->wlQueue);
    wl_buffer_add_listener(surface->ctx.wlStreamResource,
                           &wl_buffer_listener,
                           surface);
    ret = wlEglRoundtrip(display, display->wlQueue);
    if (ret < 0) {
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
    surface->ctx.eglStream =
        data->egl.createStream(display->devDpy->eglDisplay, eglAttribs);
    if (surface->ctx.eglStream == EGL_NO_STREAM_KHR) {
       err = data->egl.getError();
       goto fail;
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
create_surface_stream(WlEglSurface *surface)
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
        err = create_surface_stream_remote(surface, EGL_FALSE);
    }
#endif

    if ((err != EGL_SUCCESS) &&
        display->caps.stream_fd &&
        display->exts.stream_cross_process_fd) {
        err = create_surface_stream_fd(surface);
    }

#ifdef EGL_NV_stream_remote
    if ((err != EGL_SUCCESS) &&
        display->caps.stream_inet &&
        display->exts.stream_remote) {
        err = create_surface_stream_remote(surface, EGL_TRUE);
    }
#endif

    return err;
}

static EGLint
create_surface_context(WlEglSurface *surface)
{
    WlEglDisplay         *display     = surface->wlEglDpy;
    WlEglPlatformData    *data        = display->data;
    struct wl_egl_window *window      = surface->wlEglWin;
    EGLint                synchronous = EGL_FALSE;
    EGLint                err         = EGL_SUCCESS;

    assert(surface->ctx.eglSurface == EGL_NO_SURFACE);

    /* Width and height are the first and second attributes respectively */
    surface->attribs[1] = window->width;
    surface->attribs[3] = window->height;

    /* First, create the underlying wl_eglstream and EGLStream */
    err = create_surface_stream(surface);
    if (err != EGL_SUCCESS) {
        goto fail;
    }

    /* Then attach the wl_eglstream so the compositor connects a consumer to the
     * EGLStream */
    wl_surface_attach(window->surface,
                      surface->ctx.wlStreamResource,
                      window->dx,
                      window->dy);
    wl_surface_commit(window->surface);
    wl_proxy_set_queue((struct wl_proxy *)window->surface, display->wlQueue);
    if (wlEglRoundtrip(display, display->wlQueue) < 0) {
        err = EGL_BAD_ALLOC;
        goto fail;
    }
    wl_proxy_set_queue((struct wl_proxy *)window->surface, NULL);

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
    surface->width = window->width;
    surface->height = window->height;
    surface->dx = window->dx;
    surface->dy = window->dy;

    return EGL_SUCCESS;

fail:
    destroy_surface_context(surface, &surface->ctx);
    return err;
}

static void
resize_callback(struct wl_egl_window *window, void *data)
{
    WlEglDisplay      *display = NULL;
    WlEglPlatformData *pData   = NULL;
    WlEglSurface      *surface = (WlEglSurface *)data;
    WlEglSurfaceCtx   *ctx     = NULL;
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

        /* Defer surface context destruction until we make sure compositor
         * doesn't need it anymore (i.e. upon stream release) */
        ctx = malloc(sizeof(WlEglSurfaceCtx));
        if (ctx) {
            memcpy(ctx, &surface->ctx, sizeof(*ctx));
            wl_list_insert(&surface->oldCtxList, &ctx->link);
        }
        surface->ctx.wlStreamResource = NULL;
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
    WlEglDisplay *display = (WlEglDisplay*)dpy;
    WlEglSurface *surface = (WlEglSurface*)eglSurface;
    WlEglSurfaceCtx *ctx, *next;

    if (!wlEglIsWlEglDisplay(display)) {
        return EGL_BAD_DISPLAY;
    }

    if (!wlEglIsWlEglSurface(surface)) {
        return EGL_BAD_SURFACE;
    }

    if (!surface->ctx.isOffscreen) {
        // Force damage thread to exit before invalidating the window objects
        finish_wl_eglstream_damage_thread(surface, &surface->ctx, 1);

        if (wlEglIsWaylandDisplay(display->nativeDpy) &&
            wlEglIsWaylandWindowValid(surface->wlEglWin)) {
            wl_surface_attach(surface->wlEglWin->surface, NULL, 0, 0);
            wl_surface_commit(surface->wlEglWin->surface);
            wlEglRoundtrip(display, display->wlQueue);

            surface->wlEglWin->private = NULL;
            surface->wlEglWin->resize_callback = NULL;
        }

        wl_list_for_each_safe(ctx, next, &surface->oldCtxList, link) {
            destroy_surface_context(surface, ctx);
            wl_list_remove(&ctx->link);
            free(ctx);
        }

        free(surface->attribs);
    }

    destroy_surface_context(surface, &surface->ctx);

    wl_list_remove(&surface->link);
    free(surface);

    return EGL_SUCCESS;
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
    if (window->private != NULL) {
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

    window->private = surface;
    window->resize_callback = resize_callback;
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
        err = data->egl.getError();
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
    wlEglSetError(data, err);
    return EGL_NO_SURFACE;
}

EGLBoolean wlEglDestroySurfaceHook(EGLDisplay dpy, EGLSurface eglSurface)
{
    WlEglDisplay *display = (WlEglDisplay*)dpy;
    EGLBoolean    err     = EGL_SUCCESS;

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
