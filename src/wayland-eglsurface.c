/*
 * Copyright (c) 2014-2022, NVIDIA CORPORATION. All rights reserved.
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

#include "wayland-eglsurface-internal.h"
#include "wayland-eglstream-client-protocol.h"
#include "wayland-eglstream-controller-client-protocol.h"
#include "linux-dmabuf-unstable-v1-client-protocol.h"
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
#include <poll.h>
#include <errno.h>
#include <libdrm/drm_fourcc.h>

#define WL_EGL_WINDOW_DESTROY_CALLBACK_SINCE 3

enum BufferReleaseThreadEvents {
    BUFFER_RELEASE_THREAD_EVENT_TERMINATE,
};

enum BufferReleasePipeEndpoints {
    BUFFER_RELEASE_PIPE_READ  = 0,
    BUFFER_RELEASE_PIPE_WRITE = 1
};

static WlEglStreamImage *
pop_acquired_image(WlEglSurface *surface);

static void
remove_surface_image(WlEglDisplay *display,
                     WlEglSurface *surface,
                     EGLImageKHR eglImage);

EGLBoolean wlEglIsWlEglSurfaceForDisplay(WlEglDisplay *display, WlEglSurface *surface)
{
    WlEglSurface *surf;

    wl_list_for_each(surf, &display->wlEglSurfaceList, link) {
        if (surf == surface) {
            return EGL_TRUE;
        }
    }

    return EGL_FALSE;
}

EGLBoolean wlEglIsWaylandWindowValid(struct wl_egl_window *window)
{
    struct wl_surface *surface = NULL;

    if (!window || !wlEglMemoryIsReadable(window, sizeof (*window))) {
        return EGL_FALSE;
    }

    surface = (struct wl_surface *)window->version;
    if (!wlEglMemoryIsReadable(surface, sizeof (void *))) {
        surface = window->surface;
        if (!wlEglMemoryIsReadable(surface, sizeof (void *))) {
            return EGL_FALSE;
        }
    }
    return wlEglCheckInterfaceType((struct wl_object *)surface,
                                   "wl_surface");
}

static void
wayland_throttleCallback(void *data,
                         struct wl_callback *callback,
                         uint32_t time)
{
    WlEglSurface *surface = (WlEglSurface *)data;

    (void) time;

    if (surface->throttleCallback != NULL) {

        wl_callback_destroy(callback);
        surface->throttleCallback = NULL;
    }
}

static const struct wl_callback_listener throttle_listener = {
    wayland_throttleCallback
};

void wlEglCreateFrameSync(WlEglSurface *surface)
{
    struct wl_surface *wrapper = NULL;

    assert(surface->wlEventQueue);
    if (surface->swapInterval > 0) {
        wrapper = wl_proxy_create_wrapper(surface->wlSurface);
        wl_proxy_set_queue((struct wl_proxy *)wrapper, surface->wlEventQueue);
        surface->throttleCallback = wl_surface_frame(wrapper);
        wl_proxy_wrapper_destroy(wrapper); /* Done with wrapper */
        if (wl_callback_add_listener(surface->throttleCallback,
                                     &throttle_listener, surface) == -1) {
            return;
        }
    }
}

EGLint wlEglWaitFrameSync(WlEglSurface *surface)
{

    WlEglDisplay *display = surface->wlEglDpy;
    struct wl_event_queue *queue = surface->wlEventQueue;
    int ret = 0;

    assert(queue || surface->throttleCallback == NULL);
    while (ret != -1 && surface->throttleCallback != NULL) {
        ret = wl_display_dispatch_queue(display->nativeDpy, queue);
    }

    return EGL_SUCCESS;
}

EGLBoolean
wlEglSendDamageEvent(WlEglSurface *surface, struct wl_event_queue *queue)
{
    struct wl_display *wlDpy = surface->wlEglDpy->nativeDpy;

    if (surface->ctx.wlStreamResource) {
        /* Attach same buffer to indicate new content for the surface is
         * made available by the client */
        wl_surface_attach(surface->wlSurface,
                          surface->ctx.wlStreamResource,
                          surface->dx,
                          surface->dy);
    } else {
        WlEglStreamImage *image;

        if (wlEglHandleImageStreamEvents(surface) != EGL_SUCCESS) {
            return EGL_FALSE;
        }

        image = pop_acquired_image(surface);
        if (image) {
            surface->ctx.currentBuffer = image->buffer;
            image->attached = EGL_TRUE;
        }

        wl_surface_attach(surface->wlSurface,
                          surface->ctx.currentBuffer,
                          surface->dx,
                          surface->dy);
    }

    wl_surface_damage(surface->wlSurface, 0, 0,
                      surface->width, surface->height);
    wl_surface_commit(surface->wlSurface);
    surface->ctx.isAttached = EGL_TRUE;

    return (wl_display_roundtrip_queue(wlDpy,
                                       queue) >= 0) ? EGL_TRUE : EGL_FALSE;
}

static void*
damage_thread(void *args)
{
    WlEglSurface          *surface = (WlEglSurface*)args;
    WlEglDisplay          *display = surface->wlEglDpy;
    WlEglPlatformData     *data    = display->data;
    struct wl_event_queue *queue   = wl_display_create_queue(
                                        display->nativeDpy);
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
                if (display->devDpy->exts.stream_flush) {
                    data->egl.streamFlush(display->devDpy->eglDisplay,
                                          surface->ctx.eglStream);
                }

                pthread_mutex_lock(&surface->mutexFrameSync);

                wlEglCreateFrameSync(surface);

                ok = wlEglSendDamageEvent(surface, queue);
                surface->ctx.framesProcessed++;

                pthread_cond_signal(&surface->condFrameSync);

                pthread_mutex_unlock(&surface->mutexFrameSync);
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

    wl_event_queue_destroy(queue);
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
            pthread_join(ctx->damageThreadId, NULL);
            ctx->damageThreadId = (pthread_t)0;
        }
        data->egl.destroySync(display->devDpy->eglDisplay,
                              ctx->damageThreadSync);
        ctx->damageThreadSync = EGL_NO_SYNC_KHR;
    }
}

static void*
buffer_release_thread(void *args)
{
    WlEglSurface           *surface = (WlEglSurface*)args;
    WlEglDisplay           *display = surface->wlEglDpy;
    struct wl_display      *wlDpy = display->nativeDpy;
    struct wl_event_queue  *queue = surface->wlBufferEventQueue;
    struct pollfd           pfds[2];
    const int               fd = wl_display_get_fd(wlDpy);
    int                     res;
    uint8_t                 cmd;

    while (1) {
        /* First clear out any previously-read events on the queue */
        wl_display_dispatch_queue_pending(wlDpy, queue);

        /*
         * Now block until more events are present on the wire, then
         * read them. Also, watch for messages from the app thread.
         */
        if ((wl_display_prepare_read_queue(wlDpy, queue) < 0)) {
            if (errno == EAGAIN) {
                continue;
            } else {
                return NULL;
            }
        }

        memset(&pfds, 0, sizeof(pfds));
        pfds[0].fd = fd;
        pfds[0].events = POLLIN;
        pfds[1].fd =
            surface->bufferReleaseThreadPipe[BUFFER_RELEASE_PIPE_READ];
        pfds[1].events = POLLIN;
        res = poll(&pfds[0], sizeof(pfds) / sizeof(pfds[0]), 1000);

        if (res <= 0) {
            wl_display_cancel_read(wlDpy);
            continue;
        }

        if (pfds[1].revents & POLLIN) {
            if (read(pfds[1].fd, &cmd, sizeof(cmd) != sizeof(cmd))) {
                /* Reading an event from the app side failed. Bail. */
                wl_display_cancel_read(wlDpy);
                return NULL;
            }

            switch (cmd) {
            default:
                /*
                 * Unknown command from the app side. Treat it like a
                 * termination request.
                 *
                 * fall through.
                 */
            case BUFFER_RELEASE_THREAD_EVENT_TERMINATE:
                wl_display_cancel_read(wlDpy);
                return NULL;
            }
        }

        if (pfds[0].revents & POLLIN) {
            if (wl_display_read_events(wlDpy) < 0) {
                return NULL;
            }
        } else {
            wl_display_cancel_read(wlDpy);
        }
    }

    return NULL;
}

static EGLint setup_wl_buffer_release_thread(WlEglSurface *surface)
{
    int ret;

    if (pipe(surface->bufferReleaseThreadPipe)) {
        return EGL_BAD_ALLOC;
    }

    surface->wlBufferEventQueue =
        wl_display_create_queue(surface->wlEglDpy->nativeDpy);

    ret = pthread_create(&surface->bufferReleaseThreadId, NULL,
                         buffer_release_thread, (void*)surface);
    if (ret != 0) {
        close(surface->bufferReleaseThreadPipe[BUFFER_RELEASE_PIPE_WRITE]);
        surface->bufferReleaseThreadPipe[BUFFER_RELEASE_PIPE_WRITE] = -1;
        close(surface->bufferReleaseThreadPipe[BUFFER_RELEASE_PIPE_READ]);
        surface->bufferReleaseThreadPipe[BUFFER_RELEASE_PIPE_READ] = -1;
        wl_event_queue_destroy(surface->wlBufferEventQueue);
        surface->wlBufferEventQueue = NULL;
        return EGL_BAD_ALLOC;
    }

    return EGL_SUCCESS;
}

static void
finish_wl_buffer_release_thread(WlEglSurface *surface)
{
    uint8_t cmd = BUFFER_RELEASE_THREAD_EVENT_TERMINATE;

    if (surface->wlBufferEventQueue) {
        if (write(surface->bufferReleaseThreadPipe[BUFFER_RELEASE_PIPE_WRITE],
                  &cmd, sizeof(cmd)) != sizeof(cmd)) {
            /* The thread is not going to terminate gracefully. */
            pthread_cancel(surface->bufferReleaseThreadId);
        }
        pthread_join(surface->bufferReleaseThreadId, NULL);
        surface->bufferReleaseThreadId = (pthread_t)0;

        close(surface->bufferReleaseThreadPipe[BUFFER_RELEASE_PIPE_WRITE]);
        surface->bufferReleaseThreadPipe[BUFFER_RELEASE_PIPE_WRITE] = -1;
        close(surface->bufferReleaseThreadPipe[BUFFER_RELEASE_PIPE_READ]);
        surface->bufferReleaseThreadPipe[BUFFER_RELEASE_PIPE_READ] = -1;

        wl_event_queue_destroy(surface->wlBufferEventQueue);
        surface->wlBufferEventQueue = NULL;
    }
}

static void
destroy_stream_image(WlEglDisplay *display,
                     WlEglSurface *surface,
                     WlEglStreamImage *image)
{
    WlEglPlatformData   *data     = display->data;
    EGLDisplay           dpy      = display->devDpy->eglDisplay;

    /* Must be called with image->mutex already locked */

    assert(image->eglImage != EGL_NO_IMAGE_KHR);
    data->egl.destroyImage(dpy, image->eglImage);
    image->eglImage = EGL_NO_IMAGE_KHR;

    if (surface->ctx.currentBuffer == image->buffer) {
        surface->ctx.currentBuffer = NULL;
    }

    if (image->buffer && !image->attached) {
        wl_buffer_destroy(image->buffer);
        image->buffer = NULL;
    }

    if (!wl_list_empty(&image->acquiredLink)) {
        wl_list_remove(&image->acquiredLink);
        wl_list_init(&image->acquiredLink);
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
    uint32_t           i;

    finish_wl_eglstream_damage_thread(surface, ctx, 1);

    ctx->eglSurface       = EGL_NO_SURFACE;
    ctx->eglStream        = EGL_NO_STREAM_KHR;
    ctx->wlStreamResource = NULL;

    for (i = 0; i < ctx->numStreamImages; i++) {
        WlEglStreamImage *image = surface->ctx.streamImages[i];

        pthread_mutex_lock(&image->mutex);
        if (image->eglImage != EGL_NO_IMAGE_KHR) {
            destroy_stream_image(display, surface, image);
        }
        pthread_mutex_unlock(&image->mutex);
    }

    if (surf != EGL_NO_SURFACE) {
        data->egl.destroySurface(dpy, surf);
    }

    if (surface->ctx.isOffscreen) {
        return;
    }

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
    if (surface->ctx.isAttached && surface->ctx.wlStreamResource) {
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
    wl_list_for_each(ctx, &surface->oldCtxList, link) {
        if (ctx->wlStreamResource == buffer) {
            destroy_surface_context(surface, ctx);
            wl_list_remove(&ctx->link);
            free(ctx);
            break;
        }
    }
}

static struct wl_buffer_listener wl_buffer_listener = {
    wl_buffer_release
};

static void *
create_wl_eglstream(WlEglSurface *surface,
                    int32_t handle,
                    int32_t type,
                    struct wl_array *attribs)
{
    WlEglDisplay                *display = surface->wlEglDpy;
    struct wl_egl_window        *window  = surface->wlEglWin;
    struct wl_eglstream_display *wrapper = NULL;
    struct wl_buffer            *buffer  = NULL;
    int32_t                      width;
    int32_t                      height;

    if (!display->wlStreamDpy) {
        return NULL;
    }

    if (surface->isSurfaceProducer) {
        assert(window);
        width  = window->width;
        height = window->height;
    } else {
        width  = surface->width;
        height = surface->height;
    }

    wrapper = wl_proxy_create_wrapper(display->wlStreamDpy);
    wl_proxy_set_queue((struct wl_proxy *)wrapper, surface->wlEventQueue);

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

static EGLint create_surface_stream_fd(WlEglSurface *surface)
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
    if (display->devDpy->exts.stream_fifo_synchronous &&
        display->devDpy->exts.stream_sync &&
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
                            &wlAttribs);
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

    memset(&addr, 0, sizeof(addr));

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
                                           EGLBoolean useInet)
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
        if (!(wlAttribsData = (intptr_t *)wl_array_add(&wlAttribs,
                                                       4*sizeof(intptr_t)))) {
            err = EGL_BAD_ALLOC;
            goto fail;
        }

        /* Get host byte ordered address for sending through wayland protocol.
         * Wayland will convert back to wire format before sending. Assume a
         * local INET connection until cross partition wayland support is added.
         */
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

    if (!(wlAttribsData = (intptr_t *)wl_array_add(&wlAttribs,
                                                   2*sizeof(intptr_t)))) {
        err = EGL_BAD_ALLOC;
        goto fail;
    }

    /* If Vulkan, default Y_INVERTED to 'true'. Otherwise, assume OpenGL
     * orientation (image origin at the lower left corner), which aligns with
     * what a wayland compositor would consider 'non-y-inverted' */
    wlAttribsData[0] = WL_EGLSTREAM_ATTRIB_Y_INVERTED;
    wlAttribsData[1] =
        (intptr_t)(surface->isSurfaceProducer ? EGL_FALSE : EGL_TRUE);

    surface->ctx.wlStreamResource =
        create_wl_eglstream(surface,
                            socket[1],
                            (useInet ? WL_EGLSTREAM_HANDLE_TYPE_INET :
                                       WL_EGLSTREAM_HANDLE_TYPE_SOCKET),
                            &wlAttribs);
    if (!surface->ctx.wlStreamResource) {
        err = EGL_BAD_ALLOC;
        goto fail;
    }

    /* Need a roundtrip for the consumer's endpoint to be created before the
     * producer's */
    if (wl_display_roundtrip_queue(display->nativeDpy,
                                   surface->wlEventQueue) < 0) {
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

    wl_array_release(&wlAttribs);
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

static void
stream_local_buffer_release_callback(void *ptr, struct wl_buffer *buffer)
{
    WlEglStreamImage   *image = (WlEglStreamImage*)ptr;
    /* Safe: image->surface pointer value is const once image is initialized */
    WlEglSurface       *surface = image->surface;
    WlEglDisplay       *display = surface->wlEglDpy;
    WlEglPlatformData  *data    = display->data;

    (void)buffer; /* In case assert() compiles to nothing */
    assert(image->buffer == NULL || image->buffer == buffer);

    pthread_mutex_lock(&image->mutex);
    image->attached = EGL_FALSE;

    if (image->eglImage != EGL_NO_IMAGE_KHR) {
        data->egl.streamReleaseImage(display->devDpy->eglDisplay,
                                     surface->ctx.eglStream,
                                     image->eglImage,
                                     EGL_NO_SYNC_KHR);
    } else if (image->buffer) {
        /*
         * If the image has been destroyed while the buffer was attached
         * the buffer destruction was deferred. Clean it up now.
         */
        wl_buffer_destroy(image->buffer);
        image->buffer = NULL;
    }

    pthread_mutex_unlock(&image->mutex);
}

static const struct wl_buffer_listener stream_local_buffer_listener = {
    stream_local_buffer_release_callback,
};

static EGLint
acquire_surface_image(WlEglDisplay *display, WlEglSurface *surface)
{
    WlEglPlatformData  *data        = display->data;
    EGLDisplay          dpy         = display->devDpy->eglDisplay;
    EGLImageKHR         eglImage;
    WlEglStreamImage   *image = NULL;
    struct zwp_linux_dmabuf_v1 *wrapper = NULL;
    struct zwp_linux_buffer_params_v1 *params;
    EGLuint64KHR        modifier;
    int                 format;
    int                 planes;
    EGLint              stride;
    EGLint              offset;
    uint32_t            i;
    int                 fd;

    if (!data->egl.streamAcquireImage(dpy,
                                      surface->ctx.eglStream,
                                      &eglImage,
                                      EGL_NO_SYNC_KHR)) {
        return EGL_BAD_SURFACE;
    }

    for (i = 0; i < surface->ctx.numStreamImages; i++) {
        if (surface->ctx.streamImages[i]->eglImage == eglImage) {
            image = surface->ctx.streamImages[i];
            break;
        }
    }

    if (!image) {
        goto fail_release;
    }

    pthread_mutex_lock(&image->mutex);

    if (!image->buffer) {
        if (!data->egl.exportDMABUFImageQuery(dpy,
                                              eglImage,
                                              &format,
                                              &planes,
                                              &modifier)) {
            goto fail_release;
        }

        assert(planes == 1); /* XXX support planar formats */

        if (!data->egl.exportDMABUFImage(dpy,
                                         eglImage,
                                         &fd,
                                         &stride,
                                         &offset)) {
            goto fail_release;
        }

        wrapper = wl_proxy_create_wrapper(display->wlDmaBuf);
        wl_proxy_set_queue((struct wl_proxy *)wrapper, surface->wlBufferEventQueue);

        params = zwp_linux_dmabuf_v1_create_params(wrapper);

        zwp_linux_buffer_params_v1_add(params,
                                       fd,
                                       0, /* XXX support planar formats */
                                       offset,
                                       stride,
                                       modifier >> 32,
                                       modifier & 0xffffffff);

        /*
         * Before sending the format, check if we are ignoring alpha due to a
         * surface attribute.
         */
        if (surface->presentOpaque) {
            /*
             * We are ignoring alpha, so we need to find a DRM_FORMAT_* that is equivalent to
             * the current format, but ignores the alpha. i.e. RGBA -> RGBX
             *
             * There is also only one format with alpha that we expose on wayland: ARGB8888. If
             * the format does not match this, silently ignore it as the app must be mistakenly
             * using EGL_PRESENT_OPAQUE_EXT on an already opaque surface.
             */
            if (format == DRM_FORMAT_ARGB8888) {
                format = DRM_FORMAT_XRGB8888;
            }
        }

        image->buffer = zwp_linux_buffer_params_v1_create_immed(params,
                                                                surface->width,
                                                                surface->height,
                                                                format, 0);

        zwp_linux_buffer_params_v1_destroy(params);
        wl_proxy_wrapper_destroy(wrapper); /* Done with wrapper */
        close(fd);

        if (!image->buffer) {
            goto fail_release;
        }

        if (wl_buffer_add_listener(image->buffer,
                                   &stream_local_buffer_listener,
                                   image) == -1) {
            wl_buffer_destroy(image->buffer);
            image->buffer = NULL;
            goto fail_release;
        }
    }

    /* Add image to the end of the acquired images list */
    wl_list_insert(surface->ctx.acquiredImages.prev, &image->acquiredLink);

    pthread_mutex_unlock(&image->mutex);

    return EGL_SUCCESS;

fail_release:
    /* Release the image back to the stream */
    data->egl.streamReleaseImage(dpy,
                                 surface->ctx.eglStream,
                                 eglImage,
                                 EGL_NO_SYNC_KHR);

    if (image) {
        /* Release the image lock */
        pthread_mutex_unlock(&image->mutex);
    }

    return EGL_BAD_SURFACE;
}

static void
remove_surface_image(WlEglDisplay *display,
                     WlEglSurface *surface,
                     EGLImageKHR eglImage)
{
    WlEglStreamImage    *image;
    uint32_t             i;
    EGLBoolean           found = EGL_FALSE;

    for (i = 0; i < surface->ctx.numStreamImages && found == EGL_FALSE; i++) {
        image = surface->ctx.streamImages[i];
        pthread_mutex_lock(&image->mutex);
        if (image->eglImage == eglImage) {
            found = EGL_TRUE;
            destroy_stream_image(display, surface, image);
        }
        pthread_mutex_unlock(&image->mutex);
    }
}

static WlEglStreamImage *
pop_acquired_image(WlEglSurface *surface)
{
    WlEglStreamImage *image;

    wl_list_for_each(image, &surface->ctx.acquiredImages, acquiredLink) {
        /* Safe only because the iteration is aborted by returning */
        wl_list_remove(&image->acquiredLink);
        wl_list_init(&image->acquiredLink);
        return image;
    }

    /* List is empty. Return NULL. */
    return NULL;
}

static EGLint
add_surface_image(WlEglDisplay *display, WlEglSurface *surface)
{
    WlEglPlatformData   *data     = display->data;
    EGLDisplay           dpy      = display->devDpy->eglDisplay;
    WlEglStreamImage   **newImages;
    WlEglStreamImage    *image;
    EGLImageKHR          eglImage;
    uint32_t             i;

    for (i = 0; i < surface->ctx.numStreamImages; i++) {
        image = surface->ctx.streamImages[i];
        pthread_mutex_lock(&image->mutex);
        if ((image->eglImage == EGL_NO_IMAGE_KHR) && !image->buffer) {
            eglImage = image->eglImage =
                data->egl.createImage(dpy,
                                      EGL_NO_CONTEXT,
                                      EGL_STREAM_CONSUMER_IMAGE_NV,
                                      (EGLClientBuffer)surface->ctx.eglStream,
                                      NULL);

            pthread_mutex_unlock(&image->mutex);

            if (eglImage != EGL_NO_IMAGE_KHR) {
                return EGL_SUCCESS;
            } else {
                return EGL_BAD_ALLOC;
            }
        }
        pthread_mutex_unlock(&image->mutex);
    }

    newImages = realloc(surface->ctx.streamImages,
                        sizeof(newImages[0]) *
                        (surface->ctx.numStreamImages + 1));

    if (!newImages) {
        return EGL_BAD_ALLOC;
    }

    surface->ctx.streamImages = newImages;

    newImages[i] = calloc(1, sizeof(*newImages[i]));

    if (!surface->ctx.streamImages[i]) {
        goto free_image;
    }

    newImages[i]->surface = surface;

    /*
     * No need to lock the mutex after allocating because numStreamImages is not
     * incremented until after the image itself is initialized. Also, because
     * numStreamImages is not incremented until initialization is complete, the
     * image must be freed if initialization fails beyond this point to avoid
     * leaks.
     */
    if (!wlEglInitializeMutex(&newImages[i]->mutex)) {
        goto free_image;
    }

    wl_list_init(&newImages[i]->acquiredLink);
    newImages[i]->eglImage =
        data->egl.createImage(dpy,
                              EGL_NO_CONTEXT,
                              EGL_STREAM_CONSUMER_IMAGE_NV,
                              (EGLClientBuffer)surface->ctx.eglStream,
                              NULL);

    if (newImages[i]->eglImage == EGL_NO_IMAGE_KHR) {
        wlEglMutexDestroy(&newImages[i]->mutex);
        goto free_image;
    }

    surface->ctx.numStreamImages++;

    return EGL_SUCCESS;

free_image:
    free(newImages[i]);
    newImages[i] = NULL;

    return EGL_BAD_ALLOC;
}

EGLint wlEglHandleImageStreamEvents(WlEglSurface *surface)
{
    WlEglDisplay         *display = surface->wlEglDpy;
    EGLDisplay            dpy     = display->devDpy->eglDisplay;
    WlEglPlatformData    *data    = display->data;
    EGLAttrib             aux;
    EGLenum               event;
    EGLint                err = EGL_SUCCESS;

    if (surface->ctx.wlStreamResource) {
        /* Not a local stream */
        return err;
    }

    while (1) {
        err = data->egl.queryStreamConsumerEvent(dpy,
                                                 surface->ctx.eglStream,
                                                 0,
                                                 &event,
                                                 &aux);

        if (err == EGL_TRUE) {
            err = EGL_SUCCESS;
        } else if (err == EGL_TIMEOUT_EXPIRED) {
            err = EGL_SUCCESS;
            break;
        } else if (err == EGL_FALSE) {
            /* XXX Pick the right error code */
            err = EGL_BAD_SURFACE;
            break;
        } else {
            break;
        }

        switch (event) {
        case EGL_STREAM_IMAGE_AVAILABLE_NV:
            err = acquire_surface_image(display, surface);
            break;
        case EGL_STREAM_IMAGE_ADD_NV:
            err = add_surface_image(display, surface);
            break;

        case EGL_STREAM_IMAGE_REMOVE_NV:
            remove_surface_image(display, surface, (EGLImageKHR)aux);
            break;

        default:
            assert(!"Unhandled EGLImage stream consumer event");
        }
    }

    return err;
}

static EGLint create_surface_stream_local(WlEglSurface *surface)
{
    WlEglDisplay         *display = surface->wlEglDpy;
    WlEglPlatformData    *data    = display->data;
    EGLDisplay            dpy     = display->devDpy->eglDisplay;
    EGLint                eglAttribs[] = {
        EGL_STREAM_FIFO_LENGTH_KHR, surface->fifoLength,
        EGL_NONE,                   EGL_NONE,
        EGL_NONE
    };
    /* EGLImage stream consumer will be configured with linear modifier
     * if __NV_PRIME_RENDER_OFFLOAD was set during initialization. */
    uint64_t linearModifier = DRM_FORMAT_MOD_LINEAR;
    EGLint err = EGL_SUCCESS;

    /* We don't have any mechanism to check whether the compositor is going to
     * use this surface for composition or not when using local streams, so
     * just enable FIFO_SYNCHRONOUS if the extensions are supported.
     * Note the use of this mechanism makes the optional sync parameter
     * passed to eglStreamAcquireImageNV() redundant, so that mechanism is not
     * used in this library.
     */
    if (display->devDpy->exts.stream_fifo_synchronous &&
        display->devDpy->exts.stream_sync &&
        surface->fifoLength > 0) {
        eglAttribs[2] = EGL_STREAM_FIFO_SYNCHRONOUS_NV;
        eglAttribs[3] = EGL_TRUE;
    }

    /* First, create the EGLStream */
    surface->ctx.eglStream =
        data->egl.createStream(dpy, eglAttribs);
    if (surface->ctx.eglStream == EGL_NO_STREAM_KHR) {
        err = data->egl.getError();
        goto fail;
    }

    /* Now create the local EGLImage consumer */
    if (!data->egl.streamImageConsumerConnect(dpy,
                                              surface->ctx.eglStream,
                                              display->primeRenderOffload ? 1 : 0,
                                              &linearModifier,
                                              NULL)) {
        err = data->egl.getError();
        goto fail;
    }

    wl_list_init(&surface->ctx.acquiredImages);

    if (!surface->wlBufferEventQueue) {
        /*
         * Local stream contexts need a private wayland queue used by a separate
         * thread that can process buffer release events even the application
         * thread is blocking in the EGL library for stream frame acquisition
         * while swapping the producer surface. The thread and queue are owned
         * by the surface because they need to outlive the context when resizing
         * the surface while a buffer is still attached, but they are
         * initialized lazily here to avoid incuring the cost of an extra unused
         * thread and two pipe file descriptors per surface when they are not
         * needed.
         */
        if (EGL_SUCCESS != setup_wl_buffer_release_thread(surface)) {
            goto fail;
        }
    }

    return EGL_SUCCESS;

fail:
    destroy_surface_context(surface, &surface->ctx);
    return err;
}

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
     *    1. Local stream + dma-buf
     *    2. Cross-process unix sockets
     *    3. Cross-process FD
     *    4. Cross-process inet sockets
     */
#ifdef EGL_NV_stream_consumer_eglimage
    if ((err != EGL_SUCCESS) &&
        display->devDpy->exts.stream_consumer_eglimage &&
        display->devDpy->exts.image_dma_buf_export &&
        display->wlDmaBuf) {
        err = create_surface_stream_local(surface);
    }
#endif

#ifdef EGL_NV_stream_remote
    if ((err != EGL_SUCCESS) &&
        display->caps.stream_socket &&
        display->devDpy->exts.stream_remote) {
        err = create_surface_stream_remote(surface, EGL_FALSE);
    }
#endif

    if ((err != EGL_SUCCESS) &&
        display->caps.stream_fd &&
        display->devDpy->exts.stream_cross_process_fd) {
        err = create_surface_stream_fd(surface);
    }

#ifdef EGL_NV_stream_remote
    if ((err != EGL_SUCCESS) &&
        display->caps.stream_inet &&
        display->devDpy->exts.stream_remote) {
        err = create_surface_stream_remote(surface, EGL_TRUE);
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
    int                    winWidth    = 0;
    int                    winHeight   = 0;
    int                    winDx       = 0;
    int                    winDy       = 0;
    EGLint                 synchronous = EGL_FALSE;
    EGLint                 err         = EGL_SUCCESS;
    struct wl_array        wlAttribs;
    intptr_t              *wlAttribsData;

    assert(surface->ctx.eglSurface == EGL_NO_SURFACE);
    if (surface->isSurfaceProducer) {
        winWidth  = window->width;
        winHeight = window->height;
        winDx     = window->dx;
        winDy     = window->dy;

        /* Width and height are the first and second attributes respectively */
        surface->attribs[1] = winWidth;
        surface->attribs[3] = winHeight;
    } else {
        winWidth  = surface->width;
        winHeight = surface->height;
        winDx     = surface->dx;
        winDy     = surface->dy;
    }

    /* First, create the underlying wl_eglstream and EGLStream */
    err = create_surface_stream(surface);
    if (err != EGL_SUCCESS) {
        goto fail;
    }

    /* If the stream has a server component, attach the wl_eglstream so the
     * compositor connects a consumer to the EGLStream */
    if (surface->ctx.wlStreamResource) {
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
                wl_array_release(&wlAttribs);
            } else {
                wl_eglstream_controller_attach_eglstream_consumer(display->wlStreamCtl,
                                                                  surface->wlSurface,
                                                                  surface->ctx.wlStreamResource);
            }
        } else {
            wl_surface_attach(surface->wlSurface,
                              surface->ctx.wlStreamResource,
                              winDx,
                              winDy);
            wl_surface_commit(surface->wlSurface);

            /* Since we are using the legacy method of overloading wl_surface_attach
             * in order to create the server-side EGLStream here, the compositor
             * will actually take this as a new buffer. We mark it as 'attached'
             * because whenever a new wl_surface_attach request is issued, the
             * compositor will emit back a wl_buffer_release event, and we will
             * destroy the context then. */
            surface->ctx.isAttached = EGL_TRUE;
        }

        if (wl_display_roundtrip_queue(display->nativeDpy,
                                       surface->wlEventQueue) < 0) {
            err = EGL_BAD_ALLOC;
            goto fail;
        }
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
                    display->devDpy->exts.stream_fifo_synchronous &&
                    display->devDpy->exts.stream_sync &&
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
        surface->width = winWidth;
        surface->height = winHeight;
        surface->dx = winDx;
        surface->dy = winDy;
        window->attached_width = winWidth;
        window->attached_height = winHeight;
    }

    return EGL_SUCCESS;

fail:
    destroy_surface_context(surface, &surface->ctx);
    finish_wl_buffer_release_thread(surface);
    return err;
}

WL_EXPORT
EGLStreamKHR wlEglGetSurfaceStreamExport(WlEglSurface *surface)
{
    if (!surface)
        return EGL_NO_STREAM_KHR;

    return surface->ctx.eglStream;
}

WL_EXPORT
WlEglSurface *wlEglCreateSurfaceExport(EGLDisplay dpy,
                                       int width,
                                       int height,
                                       struct wl_surface *native_surface,
                                       int fifo_length)
{
    WlEglDisplay *display = (WlEglDisplay *)wlEglAcquireDisplay(dpy);
    WlEglSurface *surface = NULL;

    if (!display) {
        return NULL;
    }

    pthread_mutex_lock(&display->mutex);

    surface = calloc(1, sizeof (*surface));
    if (!surface) {
        goto fail;
    }

    surface->wlEglDpy = display;
    surface->width = width;
    surface->height = height;
    surface->wlSurface = native_surface;
    surface->fifoLength = fifo_length;
    surface->swapInterval = fifo_length > 0 ? 1 : 0;

    // Create per surface wayland queue
    surface->wlEventQueue = wl_display_create_queue(display->nativeDpy);
    surface->refCount = 1;

    if (!wlEglInitializeMutex(&surface->mutexLock)) {
        goto fail;
    }

    if (!wlEglInitializeMutex(&surface->mutexFrameSync)) {
        pthread_mutex_unlock(&display->mutex);
        wlEglReleaseDisplay(display);
        return EGL_FALSE;
    }

    if (pthread_cond_init(&surface->condFrameSync, NULL)) {
        pthread_mutex_unlock(&display->mutex);
        wlEglReleaseDisplay(display);
        return EGL_FALSE;
    }

    if (create_surface_context(surface) != EGL_SUCCESS) {
        wl_event_queue_destroy(surface->wlEventQueue);
        goto fail;
    }

    wl_list_insert(&display->wlEglSurfaceList, &surface->link);
    wl_list_init(&surface->oldCtxList);

    if (surface->ctx.wlStreamResource) {
        /* Set client's pendingSwapIntervalUpdate for updating client's
         * swapinterval
         */
        surface->pendingSwapIntervalUpdate = EGL_TRUE;
    }

    pthread_mutex_unlock(&display->mutex);
    wlEglReleaseDisplay(display);
    return surface;

fail:
    pthread_mutex_unlock(&display->mutex);
    wlEglReleaseDisplay(display);
    free(surface);
    return NULL;
}

void
wlEglResizeSurface(WlEglDisplay *display,
                   WlEglPlatformData *pData,
                   WlEglSurface *surface)
{
    EGLint err = EGL_SUCCESS;

    // If a damage thread is in use, wait for it to finish processing all
    //   pending frames
    finish_wl_eglstream_damage_thread(surface, &surface->ctx, 0);

    discard_surface_context(surface);
    surface->isResized = EGL_FALSE;
    surface->ctx.wlStreamResource = NULL;
    surface->ctx.isAttached = EGL_FALSE;
    surface->ctx.eglSurface = EGL_NO_SURFACE;
    surface->ctx.eglStream = EGL_NO_STREAM_KHR;
    surface->ctx.damageThreadSync = EGL_NO_SYNC_KHR;
    surface->ctx.damageThreadId = (pthread_t)0;

    err = create_surface_context(surface);
    if (err == EGL_SUCCESS) {
        /* This looks like a no-op, but we've replaced the surface's internal
         * handle with a new surface, so we need to make it current again. */
        pData->egl.makeCurrent(display,
                               pData->egl.getCurrentSurface(EGL_DRAW),
                               pData->egl.getCurrentSurface(EGL_READ),
                               pData->egl.getCurrentContext());

        if (surface->ctx.wlStreamResource) {
            /* Set client's pendingSwapIntervalUpdate for updating client's
             * swapinterval
             */
            surface->pendingSwapIntervalUpdate = EGL_TRUE;
        }
    }
}

static void
resize_callback(struct wl_egl_window *window, void *data)
{
    WlEglDisplay      *display = NULL;
    WlEglPlatformData *pData;
    WlEglSurface      *surface = (WlEglSurface *)data;

    if (!window || !surface) {
        return;
    }

    display = surface->wlEglDpy;
    if (!wlEglIsWaylandDisplay(display->nativeDpy) ||
        !wlEglIsWaylandWindowValid(surface->wlEglWin)) {
        return;
    }

    pData = display->data;

    pthread_mutex_lock(&surface->mutexLock);

    /* Resize stream only if window geometry has changed */
    if ((surface->width != window->width) ||
        (surface->height != window->height) ||
        (surface->dx != window->dx) ||
        (surface->dy != window->dy)) {
            if (surface == pData->egl.getCurrentSurface(EGL_DRAW) ||
                surface == pData->egl.getCurrentSurface(EGL_READ)) {
                wlEglResizeSurface(display, pData, surface);
            } else {
                surface->isResized = EGL_TRUE;
            }
    }
    
    pthread_mutex_unlock(&surface->mutexLock);
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

    case EGL_PRESENT_OPAQUE_EXT:
        return EGL_TRUE;

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
            if (attribs[i] == EGL_PRESENT_OPAQUE_EXT) {
                surface->presentOpaque = EGL_TRUE;
                continue;
            }
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


EGLBoolean wlEglQuerySurfaceHook(EGLDisplay dpy, EGLSurface eglSurface,
                                 EGLint attribute, EGLint *value)
{
    WlEglDisplay *display       = wlEglAcquireDisplay(dpy);
    WlEglPlatformData *data     = NULL;
    WlEglSurface *surface       = (WlEglSurface *)eglSurface;
    EGLint ret                  = EGL_FALSE;
    EGLint err                  = EGL_SUCCESS;

    if (!display) {
        return EGL_FALSE;
    }
    data = display->data;

    if (!wlEglIsWlEglSurfaceForDisplay(display, surface)) {
        err = EGL_BAD_SURFACE;
        wlEglSetError(data, err);
        goto done;
    }

    if (attribute == EGL_PRESENT_OPAQUE_EXT) {
        *value = surface->presentOpaque;
        ret = EGL_TRUE;
        goto done;
    }

    dpy = display->devDpy->eglDisplay;
    ret = data->egl.querySurface(dpy, surface->ctx.eglSurface, attribute, value);

done:
    wlEglReleaseDisplay(display);
    return ret;
}

EGLBoolean wlEglSurfaceRef(WlEglDisplay *display, WlEglSurface *surface)
{

    if (!wlEglIsWlEglSurfaceForDisplay(display, surface) ||
        surface->wlEglDpy->initCount == 0) {
        return EGL_FALSE;
    }

    surface->refCount++;

    return EGL_TRUE;
}

void wlEglSurfaceUnref(WlEglSurface *surface)
{
    uint32_t i;

    surface->refCount--;
    if (surface->refCount > 0) {
        return;
    }

    wlEglMutexDestroy(&surface->mutexLock);

    if (!surface->ctx.isOffscreen) {
        wlEglMutexDestroy(&surface->mutexFrameSync);
        pthread_cond_destroy(&surface->condFrameSync);
    }

    for (i = 0; i < surface->ctx.numStreamImages; i++) {
        wlEglMutexDestroy(&surface->ctx.streamImages[i]->mutex);
        free(surface->ctx.streamImages[i]);
    }
    free(surface->ctx.streamImages);
    free(surface);

    return;
}

static EGLBoolean wlEglDestroySurface(EGLDisplay dpy, EGLSurface eglSurface)
{
    WlEglDisplay *display = (WlEglDisplay*)dpy;
    WlEglSurface *surface = (WlEglSurface*)eglSurface;
    WlEglSurfaceCtx       *ctx     = NULL;
    WlEglSurfaceCtx       *next    = NULL;
    WlEglStreamImage      *image;
    uint32_t i;

    if (!wlEglIsWlEglSurfaceForDisplay(display, surface) ||
        display != surface->wlEglDpy) {
        return EGL_FALSE;
    }

    wl_list_remove(&surface->link);
    surface->isDestroyed = EGL_TRUE;

    // Acquire WlEglSurface lock.
    pthread_mutex_lock(&surface->mutexLock);

    destroy_surface_context(surface, &surface->ctx);

    if (!surface->ctx.isOffscreen) {
        // We only expect a valid wlEglWin to be set when using
        // a surface created with EGL_KHR_platform_wayland.
        if (wlEglIsWaylandDisplay(display->nativeDpy) &&
            wlEglIsWaylandWindowValid(surface->wlEglWin)) {

            surface->wlEglWin->driver_private = NULL;
            surface->wlEglWin->resize_callback = NULL;
            if (surface->wlEglWinVer >= WL_EGL_WINDOW_DESTROY_CALLBACK_SINCE) {
                surface->wlEglWin->destroy_window_callback = NULL;
            }
        }
    }

    if (surface->throttleCallback != NULL) {
        wl_callback_destroy(surface->throttleCallback);
        surface->throttleCallback = NULL;
    }
    if (surface->wlEventQueue != NULL) {
        wl_event_queue_destroy(surface->wlEventQueue);
        surface->wlEventQueue = NULL;
    }

    if (!surface->ctx.isOffscreen) {
        wl_list_for_each_safe(ctx, next, &surface->oldCtxList, link) {
            destroy_surface_context(surface, ctx);
            wl_list_remove(&ctx->link);
            free(ctx);
        }

        free(surface->attribs);
    }

    for (i = 0; i < surface->ctx.numStreamImages; i++) {
        image = surface->ctx.streamImages[i];
        pthread_mutex_lock(&image->mutex);

        /*
         * Destroy any attached buffers to ensure no further buffer release
         * events are delivered after the buffer release queue and thread are
         * torn down.
         */
        if (image->buffer) {
            assert(image->attached);
            wl_buffer_destroy(image->buffer);
            image->buffer = NULL;
            image->attached = EGL_FALSE;
        }

        pthread_mutex_unlock(&image->mutex);
    }

    finish_wl_buffer_release_thread(surface);

    // Release WlEglSurface lock.
    pthread_mutex_unlock(&surface->mutexLock);

    wlEglSurfaceUnref(eglSurface);

    return EGL_TRUE;
}

static void
destroy_callback(void *data)
{
    WlEglSurface *surface = (WlEglSurface*)data;
    WlEglDisplay *display = surface->wlEglDpy;

    pthread_mutex_lock(&display->mutex);

    if (!surface || surface->wlEglDpy->initCount == 0) {
        pthread_mutex_unlock(&display->mutex);
        return;
    }

    wlEglDestroySurface((EGLDisplay)surface->wlEglDpy,
                        (EGLSurface)surface);
    pthread_mutex_unlock(&display->mutex);
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
     */

    *version = window->version;
    *surface = window->surface;

    if (wlEglMemoryIsReadable((void *)window->version,
                              sizeof (void *))) {
        *version = 0;
        *surface = (struct wl_surface *)(window->version);
    }
}

EGLSurface wlEglCreatePlatformWindowSurfaceHook(EGLDisplay dpy,
                                                EGLConfig config,
                                                void *nativeWin,
                                                const EGLAttrib *attribs)
{
    WlEglDisplay         *display = wlEglAcquireDisplay(dpy);
    WlEglPlatformData    *data    = NULL;
    WlEglSurface         *surface = NULL;
    struct wl_egl_window *window  = (struct wl_egl_window *)nativeWin;
    EGLBoolean            res     = EGL_FALSE;
    EGLint                err     = EGL_SUCCESS;
    EGLint                surfType;

    if (!display) {
        return EGL_NO_SURFACE;
    }
    pthread_mutex_lock(&display->mutex);

    data = display->data;

    if (display->initCount == 0) {
        err = EGL_NOT_INITIALIZED;
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

    res = data->egl.getConfigAttrib(dpy, config, EGL_SURFACE_TYPE, &surfType);

    if (!res || !(surfType & EGL_STREAM_BIT_KHR)) {
        err = EGL_BAD_CONFIG;
        goto fail;
    }

    if (!display->devDpy->exts.stream ||
        (!display->devDpy->exts.stream_cross_process_fd &&
         !display->devDpy->exts.stream_remote) ||
        !display->devDpy->exts.stream_producer_eglsurface) {
        err = EGL_BAD_ALLOC;
        goto fail;
    }

    surface = calloc(1, sizeof(*surface));
    if (!surface) {
        err = EGL_BAD_ALLOC;
        goto fail;
    }

    if (!wlEglInitializeMutex(&surface->mutexLock)) {
        err = EGL_BAD_ALLOC;
        goto fail;
    }

    if (!wlEglInitializeMutex(&surface->mutexFrameSync)) {
        err = EGL_BAD_ALLOC;
        goto fail;
    }

    if (pthread_cond_init(&surface->condFrameSync, NULL)) {
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
    surface->refCount = 1;
    surface->isDestroyed = EGL_FALSE;
    // FIFO_LENGTH == 1 to set FIFO mode, FIFO_LENGTH == 0 to set MAILBOX mode
    surface->fifoLength = (display->devDpy->exts.stream_fifo_synchronous &&
                           display->devDpy->exts.stream_sync) ? 1 : 0;

    // Create per surface wayland queue
    surface->wlEventQueue = wl_display_create_queue(display->nativeDpy);

    getWlEglWindowVersionAndSurface(window,
                                    &surface->wlEglWinVer,
                                    &surface->wlSurface);
    wl_list_init(&surface->oldCtxList);

    err = assignWlEglSurfaceAttribs(surface, attribs);
    if (err != EGL_SUCCESS) {
        goto fail;
    }

    err = create_surface_context(surface);
    if (err != EGL_SUCCESS) {
        goto fail;
    }

    surface->swapInterval = 1; // Default swap interval is 1

    if (surface->ctx.wlStreamResource) {
        /* Set client's pendingSwapIntervalUpdate for updating client's
         * swapinterval
         */
        surface->pendingSwapIntervalUpdate = EGL_TRUE;
    }
    window->driver_private = surface;
    window->resize_callback = resize_callback;
    if (surface->wlEglWinVer >= WL_EGL_WINDOW_DESTROY_CALLBACK_SINCE) {
        window->destroy_window_callback = destroy_callback;
    }

    wl_list_insert(&display->wlEglSurfaceList, &surface->link);
    pthread_mutex_unlock(&display->mutex);
    wlEglReleaseDisplay(display);

    return surface;

fail:
    if (surface) {
        wlEglDestroySurface(display, surface);
    }

    pthread_mutex_unlock(&display->mutex);
    wlEglReleaseDisplay(display);

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
    WlEglDisplay      *display = wlEglAcquireDisplay(dpy);
    WlEglPlatformData *data    = NULL;
    WlEglSurface      *surface = NULL;
    EGLSurface         surf    = EGL_NO_SURFACE;
    EGLint             err     = EGL_SUCCESS;

    if (!display) {
        return EGL_NO_SURFACE;
    }
    pthread_mutex_lock(&display->mutex);

    data = display->data;

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

    if (!wlEglInitializeMutex(&surface->mutexLock)) {
        err = EGL_BAD_ALLOC;
        goto fail;
    }

    surface->wlEglDpy = display;
    surface->eglConfig = config;
    surface->ctx.eglSurface = surf;
    surface->ctx.isOffscreen = EGL_TRUE;
    surface->refCount = 1;
    surface->isDestroyed = EGL_FALSE;
    wl_list_init(&surface->oldCtxList);

    wl_list_insert(&display->wlEglSurfaceList, &surface->link);
    pthread_mutex_unlock(&display->mutex);
    wlEglReleaseDisplay(display);

    return surface;

fail:
    pthread_mutex_unlock(&display->mutex);
    wlEglReleaseDisplay(display);

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
    WlEglDisplay      *display = wlEglAcquireDisplay(dpy);
    WlEglPlatformData *data    = NULL;
    WlEglSurface      *surface = NULL;
    EGLSurface         surf    = EGL_NO_SURFACE;
    EGLint             err     = EGL_SUCCESS;

    if (!display) {
        return EGL_NO_SURFACE;
    }
    pthread_mutex_lock(&display->mutex);

    data = display->data;

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

    if (!wlEglInitializeMutex(&surface->mutexLock)) {
        err = EGL_BAD_ALLOC;
        goto fail;
    }

    surface->wlEglDpy = display;
    surface->eglConfig = config;
    surface->ctx.eglSurface = surf;
    surface->ctx.isOffscreen = EGL_TRUE;
    surface->refCount = 1;
    surface->isDestroyed = EGL_FALSE;
    wl_list_init(&surface->oldCtxList);

    wl_list_insert(&display->wlEglSurfaceList, &surface->link);
    pthread_mutex_unlock(&display->mutex);
    wlEglReleaseDisplay(display);

    return surface;

fail:
    pthread_mutex_unlock(&display->mutex);
    wlEglReleaseDisplay(display);
    if (err != EGL_SUCCESS) {
        wlEglSetError(data, err);
    }
    return EGL_NO_SURFACE;
}

EGLBoolean wlEglDestroySurfaceHook(EGLDisplay dpy, EGLSurface eglSurface)
{
    WlEglDisplay *display = wlEglAcquireDisplay(dpy);
    EGLint ret = EGL_FALSE;

    if (!display) {
        return EGL_FALSE;
    }
    pthread_mutex_lock(&display->mutex);

    if (display->initCount == 0) {
        wlEglSetError(display->data, EGL_NOT_INITIALIZED);
        goto done;
    }

    ret = wlEglDestroySurface(dpy, eglSurface);
    if (!ret) {
        wlEglSetError(display->data, EGL_BAD_SURFACE);
    }

done:
    pthread_mutex_unlock(&display->mutex);
    wlEglReleaseDisplay(display);
    return ret;
}


EGLBoolean wlEglDestroyAllSurfaces(WlEglDisplay *display)
{
    WlEglSurface *surface, *next;
    EGLBoolean res = EGL_TRUE;

    wl_list_for_each_safe(surface, next, &display->wlEglSurfaceList, link) {
        if (surface->wlEglDpy == display) {
            res = wlEglDestroySurface(display, surface) && res;
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
    EGLint                       originY;

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
        if (wlStreamDpy->exts.stream_origin &&
            wlStreamDpy->data->egl.queryStream(wlStreamDpy->eglDisplay,
                                               wlStream->eglStream,
                                               EGL_STREAM_FRAME_ORIGIN_Y_NV,
                                               &originY)) {
            /* If we have an image with origin at the top, the wayland
             * compositor will consider it as y-inverted */
            *value = (int)((originY == EGL_TOP_NV) ? EGL_TRUE : EGL_FALSE);
        } else {
            /* No mechanism found to query frame orientation. Set to
             * stream's default value.*/
            *value = (int)wlStream->yInverted;
        }
        res = EGL_TRUE;
        goto done;
    }

done:
    wlExternalApiUnlock();
    return res;
}
