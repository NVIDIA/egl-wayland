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

#include "wayland-eglhandle.h"
#include "wayland-egldisplay.h"
#include "wayland-eglsurface.h"
#include "wayland-thread.h"
#include <stdlib.h>
#include <errno.h>
#include <assert.h>

WlEglPlatformData*
wlEglCreatePlatformData(int apiMajor, int apiMinor, const EGLExtDriver *driver)
{
    const char        *exts = NULL;
    WlEglPlatformData *res  = NULL;

    assert((driver != NULL) && (driver->getProcAddress != NULL));

    /* Allocate platform data and fetch EGL functions */
    res = calloc(1, sizeof(WlEglPlatformData));
    if (res == NULL) {
        return NULL;
    }

    /* Cache the EGL driver version */
#if EGL_EXTERNAL_PLATFORM_HAS(DRIVER_VERSION)
    if (EGL_EXTERNAL_PLATFORM_SUPPORTS(apiMajor, apiMinor, DRIVER_VERSION)) {
        res->egl.major = driver->major;
        res->egl.minor = driver->minor;
    }
#endif

    /* Fetch all required driver functions */
#define GET_PROC(_FIELD_, _NAME_)                           \
    do {                                                    \
        res->egl._FIELD_ = driver->getProcAddress(#_NAME_); \
        if (res->egl._FIELD_ == NULL) {                     \
            goto fail;                                      \
        }                                                   \
    } while (0)

    /* Core and basic stream functionality */
    GET_PROC(queryString,                 eglQueryString);
    GET_PROC(queryDevices,                eglQueryDevicesEXT);

    /* TODO: use eglGetPlatformDisplay instead of eglGetPlatformDisplayEXT
             if EGL 1.5 is available                                      */
    GET_PROC(getPlatformDisplay,          eglGetPlatformDisplayEXT);
    GET_PROC(initialize,                  eglInitialize);
    GET_PROC(terminate,                   eglTerminate);
    GET_PROC(chooseConfig,                eglChooseConfig);
    GET_PROC(getConfigAttrib,             eglGetConfigAttrib);

    GET_PROC(getCurrentContext,           eglGetCurrentContext);
    GET_PROC(getCurrentSurface,           eglGetCurrentSurface);
    GET_PROC(makeCurrent,                 eglMakeCurrent);

    GET_PROC(createStream,                eglCreateStreamKHR);
    GET_PROC(createStreamFromFD,          eglCreateStreamFromFileDescriptorKHR);
    GET_PROC(createStreamAttrib,          eglCreateStreamAttribNV);
    GET_PROC(getStreamFileDescriptor,     eglGetStreamFileDescriptorKHR);
    GET_PROC(createStreamProducerSurface, eglCreateStreamProducerSurfaceKHR);
    GET_PROC(createPbufferSurface,        eglCreatePbufferSurface);
    GET_PROC(destroyStream,               eglDestroyStreamKHR);
    GET_PROC(destroySurface,              eglDestroySurface);

    GET_PROC(swapBuffers,                 eglSwapBuffers);
    GET_PROC(swapBuffersWithDamage,       eglSwapBuffersWithDamageKHR);
    GET_PROC(swapInterval,                eglSwapInterval);

    GET_PROC(getError,                    eglGetError);
    GET_PROC(releaseThread,               eglReleaseThread);

#undef GET_PROC

    /* Fetch all optional driver functions */
#define GET_PROC(_FIELD_, _NAME_) \
    res->egl._FIELD_ = driver->getProcAddress(#_NAME_)

    /* Used by damage thread */
    GET_PROC(queryStream,                 eglQueryStreamKHR);
    GET_PROC(queryStreamu64,              eglQueryStreamu64KHR);
    GET_PROC(createStreamSync,            eglCreateStreamSyncNV);
    GET_PROC(clientWaitSync,              eglClientWaitSyncKHR);
    GET_PROC(signalSync,                  eglSignalSyncKHR);
    GET_PROC(destroySync,                 eglDestroySyncKHR);

    /* Stream flush */
    GET_PROC(streamFlush,                 eglStreamFlushNV);

    GET_PROC(queryDisplayAttrib,          eglQueryDisplayAttribKHR);

#undef GET_PROC

    /* Check for required EGL client extensions */
    exts = res->egl.queryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    if (!wlEglFindExtension("EGL_EXT_platform_base",   exts) ||
        !wlEglFindExtension("EGL_EXT_platform_device", exts)) {
        goto fail;
    }

    /* Cache driver imports */
    res->callbacks.setError           = driver->setError;
    res->callbacks.streamSwapInterval = driver->streamSwapInterval;

    return res;

fail:
    free(res);
    return NULL;
}

void wlEglDestroyPlatformData(WlEglPlatformData *data)
{
    free(data);
}

void* wlEglGetInternalHandleExport(EGLDisplay dpy, EGLenum type, void *handle)
{
    wlExternalApiLock();

    if ((type == EGL_OBJECT_DISPLAY_KHR) &&
        wlEglIsWlEglDisplay((WlEglDisplay *)handle)) {
        handle = (void *)(((WlEglDisplay *)handle)->devDpy->eglDisplay);
    } else if ((type == EGL_OBJECT_SURFACE_KHR) &&
               wlEglIsWlEglSurface((WlEglSurface *)handle)) {
        WlEglSurface *surface = (WlEglSurface *)handle;
        if (dpy == surface->wlEglDpy) {
            handle = (void *)(surface->ctx.eglSurface);
        } else {
            handle = NULL;
        }
    }

    wlExternalApiUnlock();

    return handle;
}
