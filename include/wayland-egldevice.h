/*
 * Copyright (c) 2014-2019, NVIDIA CORPORATION. All rights reserved.
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

#ifndef WAYLAND_EGLDEVICE_H
#define WAYLAND_EGLDEVICE_H

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <wayland-client.h>
#include "wayland-external-exports.h"
#include "wayland-eglhandle.h"

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Keeps track of an internal display.
 *
 * Since the same internal display can be shared by multiple external displays,
 * the internal displays are always created with the EGL_TRACK_REFERENCES_KHR
 * flag set. If the driver doesn't support that extension, then we keep track
 * of an initialization count to produce the same behavior.
 */
typedef struct WlEglDeviceDpyRec {
    EGLDeviceEXT eglDevice;
    EGLDisplay   eglDisplay;
    WlEglPlatformData *data;

    unsigned int initCount;
    EGLint major;
    EGLint minor;

    /* The EGL DRM device */
    dev_t dev;
    /* The EGL DRM render node */
    dev_t renderNode;

    struct {
        unsigned int stream                     : 1;
        unsigned int stream_attrib              : 1;
        unsigned int stream_cross_process_fd    : 1;
        unsigned int stream_remote              : 1;
        unsigned int stream_producer_eglsurface : 1;
        unsigned int stream_fifo_synchronous    : 1;
        unsigned int stream_sync                : 1;
        unsigned int stream_flush               : 1;
        unsigned int stream_consumer_eglimage   : 1;
        unsigned int image_dma_buf_export       : 1;
    } exts;

    struct wl_list link;
} WlEglDeviceDpy;

/**
 * Returns the WlEglDeviceDpy structure for a given device.
 * Note that the same device will always return the same WlEglDeviceDpy.
 */
WlEglDeviceDpy *wlGetInternalDisplay(WlEglPlatformData *data, EGLDeviceEXT device);

/**
 * Frees all of the WlEglDeviceDpy structures.
 */
void wlFreeAllInternalDisplays(WlEglPlatformData *data);

EGLBoolean wlInternalInitialize(WlEglDeviceDpy *devDpy);
EGLBoolean wlInternalTerminate(WlEglDeviceDpy *devDpy);

#ifdef __cplusplus
}
#endif

#endif // WAYLAND_EGLDEVICE_H
