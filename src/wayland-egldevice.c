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

#include "wayland-egldevice.h"

#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include "wayland-eglhandle.h"
#include "wayland-eglutils.h"

WlEglDeviceDpy *wlGetInternalDisplay(WlEglPlatformData *data, EGLDeviceEXT device)
{
    static const EGLint TRACK_REFS_ATTRIBS[] = {
        EGL_TRACK_REFERENCES_KHR,
        EGL_TRUE,
        EGL_NONE
    };

    WlEglDeviceDpy *devDpy = NULL;
    const EGLint *attribs = NULL;

    // First, see if we've already created an EGLDisplay for this device.
    wl_list_for_each(devDpy, &data->deviceDpyList, link) {
        if (devDpy->data == data && devDpy->eglDevice == device) {
            return devDpy;
        }
    }

    // We didn't find a matching display, so create one.
    if (data->supportsDisplayReference) {
        // Always use EGL_KHR_display_reference if the driver supports it.
        // We'll do our own refcounting so that we can work without it, but
        // setting EGL_TRACK_REFERENCES_KHR means that it's less likely that
        // something else might grab the same EGLDevice-based display and
        // call eglTerminate on it.
        attribs = TRACK_REFS_ATTRIBS;
    }

    devDpy = calloc(1, sizeof(WlEglDeviceDpy));
    if (devDpy == NULL) {
        return NULL;
    }

    devDpy->eglDevice = device;
    devDpy->data = data;
    devDpy->eglDisplay = data->egl.getPlatformDisplay(EGL_PLATFORM_DEVICE_EXT,
            device, attribs);
    if (devDpy->eglDisplay == EGL_NO_DISPLAY) {
        free(devDpy);
        return NULL;
    }

    wl_list_insert(&data->deviceDpyList, &devDpy->link);
    return devDpy;
}

static void wlFreeInternalDisplay(WlEglDeviceDpy *devDpy)
{
    if (devDpy->initCount > 0) {
        devDpy->data->egl.terminate(devDpy->eglDisplay);
    }
    wl_list_remove(&devDpy->link);
    free(devDpy);
}

void wlFreeAllInternalDisplays(WlEglPlatformData *data)
{
    WlEglDeviceDpy *devDpy, *devNext;
    wl_list_for_each_safe(devDpy, devNext, &data->deviceDpyList, link) {
        assert (devDpy->data == data);
        wlFreeInternalDisplay(devDpy);
    }
}

EGLBoolean wlInternalInitialize(WlEglDeviceDpy *devDpy)
{
    if (devDpy->initCount == 0) {
        const char *exts;

        if (!devDpy->data->egl.initialize(devDpy->eglDisplay, &devDpy->major, &devDpy->minor)) {
            return EGL_FALSE;
        }

        exts = devDpy->data->egl.queryString(devDpy->eglDisplay, EGL_EXTENSIONS);
#define CACHE_EXT(_PREFIX_, _NAME_)                                      \
        devDpy->exts._NAME_ =                                           \
            !!wlEglFindExtension("EGL_" #_PREFIX_ "_" #_NAME_, exts)

        CACHE_EXT(KHR, stream);
        CACHE_EXT(NV,  stream_attrib);
        CACHE_EXT(KHR, stream_cross_process_fd);
        CACHE_EXT(NV,  stream_remote);
        CACHE_EXT(KHR, stream_producer_eglsurface);
        CACHE_EXT(NV,  stream_fifo_synchronous);
        CACHE_EXT(NV,  stream_sync);
        CACHE_EXT(NV,  stream_flush);

#undef CACHE_EXT
    }

    devDpy->initCount++;
    return EGL_TRUE;
}

EGLBoolean wlInternalTerminate(WlEglDeviceDpy *devDpy)
{
    if (devDpy->initCount > 0) {
        if (devDpy->initCount == 1) {
            if (!devDpy->data->egl.terminate(devDpy->eglDisplay)) {
                return EGL_FALSE;
            }
        }
        devDpy->initCount--;
    }
    return EGL_TRUE;
}
