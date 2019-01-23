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

#ifndef WAYLAND_EGLDISPLAY_H
#define WAYLAND_EGLDISPLAY_H

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <wayland-client.h>
#include "wayland-external-exports.h"
#include "wayland-eglhandle.h"

#ifdef __cplusplus
extern "C" {
#endif

/* This define represents the version of the wl_eglstream_controller interface
   when the attach_eglstream_consumer_attrib() request was first available" */
#define WL_EGLSTREAM_CONTROLLER_ATTACH_EGLSTREAM_CONSUMER_ATTRIB_SINCE 2

typedef struct WlEglDeviceDpyRec {
    EGLDeviceEXT eglDevice;
    EGLDisplay   eglDisplay;

    unsigned int refCount;

    struct wl_list link;
} WlEglDeviceDpy;

typedef struct WlEglDisplayRec {
    WlEglDeviceDpy *devDpy;

    EGLBoolean         ownNativeDpy;
    struct wl_display *nativeDpy;
    struct wl_list     evtQueueList;

    struct wl_registry             *wlRegistry;
    struct wl_eglstream_display    *wlStreamDpy;
    struct wl_eglstream_controller *wlStreamCtl;
    unsigned int                    wlStreamCtlVer;
    struct {
        unsigned int stream_fd     : 1;
        unsigned int stream_inet   : 1;
        unsigned int stream_socket : 1;
    } caps;

    WlEglPlatformData *data;
    struct {
        unsigned int stream                     : 1;
        unsigned int stream_attrib              : 1;
        unsigned int stream_cross_process_fd    : 1;
        unsigned int stream_remote              : 1;
        unsigned int stream_producer_eglsurface : 1;
        unsigned int stream_fifo_synchronous    : 1;
        unsigned int stream_sync                : 1;
        unsigned int stream_flush               : 1;
        unsigned int display_reference          : 1;
    } exts;

    struct wl_list link;
    EGLBoolean useRefCount;
    unsigned int refCount;
} WlEglDisplay;

typedef struct WlEventQueueRec {
    WlEglDisplay          *display;
    struct wl_event_queue *queue;

    struct wl_list dpyLink;
    struct wl_list threadLink;
} WlEventQueue;

EGLBoolean wlEglIsValidNativeDisplayExport(void *data, void *nativeDpy);
EGLBoolean wlEglBindDisplaysHook(void *data, EGLDisplay dpy, void *nativeDpy);
EGLBoolean wlEglUnbindDisplaysHook(EGLDisplay dpy, void *nativeDpy);
EGLDisplay wlEglGetPlatformDisplayExport(void *data,
                                         EGLenum platform,
                                         void *nativeDpy,
                                         const EGLAttrib *attribs);
EGLBoolean wlEglInitializeHook(EGLDisplay dpy, EGLint *major, EGLint *minor);
EGLBoolean wlEglTerminateHook(EGLDisplay dpy);

EGLBoolean wlEglChooseConfigHook(EGLDisplay dpy,
                                 EGLint const * attribs,
                                 EGLConfig * configs,
                                 EGLint configSize,
                                 EGLint * numConfig);
EGLBoolean wlEglGetConfigAttribHook(EGLDisplay dpy,
                                    EGLConfig config,
                                    EGLint attribute,
                                    EGLint * value);

EGLBoolean wlEglIsWaylandDisplay(void *nativeDpy);
EGLBoolean wlEglIsWlEglDisplay(WlEglDisplay *display);

EGLBoolean wlEglDestroyAllDisplays(WlEglPlatformData *data);

const char* wlEglQueryStringExport(void *data,
                                   EGLDisplay dpy,
                                   EGLExtPlatformString name);

struct wl_event_queue* wlGetEventQueue(WlEglDisplay *display);
int wlEglRoundtrip(WlEglDisplay *display, struct wl_event_queue *queue);

#ifdef __cplusplus
}
#endif

#endif
