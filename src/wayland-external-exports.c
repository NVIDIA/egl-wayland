/*
 * Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.
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

#include <EGL/egl.h>
#include "wayland-external-exports.h"
#include "wayland-egldisplay.h"
#include "wayland-eglstream.h"
#include "wayland-eglsurface-internal.h"
#include "wayland-eglswap.h"
#include "wayland-eglutils.h"
#include "wayland-eglhandle.h"
#include <stdlib.h>
#include <string.h>

typedef struct WlEglHookRec {
    const char *name;
    void       *func;
} WlEglHook;

static const WlEglHook wlEglHooksMap[] = {
    /* Keep names in ascending order */
    { "eglBindWaylandDisplayWL",           wlEglBindDisplaysHook },
    { "eglChooseConfig",                   wlEglChooseConfigHook },
    { "eglCreatePbufferSurface",           wlEglCreatePbufferSurfaceHook },
    { "eglCreatePlatformPixmapSurface",    wlEglCreatePlatformPixmapSurfaceHook },
    { "eglCreatePlatformWindowSurface",    wlEglCreatePlatformWindowSurfaceHook },
    { "eglCreateStreamAttribNV",           wlEglCreateStreamAttribHook },
    { "eglCreateStreamProducerSurfaceKHR", wlEglCreateStreamProducerSurfaceHook },
    { "eglDestroySurface",                 wlEglDestroySurfaceHook },
    { "eglGetConfigAttrib",                wlEglGetConfigAttribHook },
    { "eglInitialize",                     wlEglInitializeHook },
    { "eglQueryDisplayAttribEXT",          wlEglQueryDisplayAttribHook },
    { "eglQueryDisplayAttribKHR",          wlEglQueryDisplayAttribHook },
    { "eglQueryString",                    wlEglQueryStringHook },
    { "eglQuerySurface",                   wlEglQuerySurfaceHook },
    { "eglQueryWaylandBufferWL",           wlEglQueryNativeResourceHook },
    { "eglSwapBuffers",                    wlEglSwapBuffersHook },
    { "eglSwapBuffersWithDamageKHR",       wlEglSwapBuffersWithDamageHook },
    { "eglSwapInterval",                   wlEglSwapIntervalHook },
    { "eglTerminate",                      wlEglTerminateHook },
    { "eglUnbindWaylandDisplayWL",         wlEglUnbindDisplaysHook },
};

static int hookCmp(const void *elemA, const void *elemB)
{
    const char *key = (const char *)elemA;
    const WlEglHook *hook = (const WlEglHook *)elemB;
    return strcmp(key, hook->name);
}

static void* wlEglGetHookAddressExport(void *data, const char *name)
{
    WlEglHook *hook;
    (void) data;

    hook = (WlEglHook *)bsearch((const void *)name,
                                (const void *)wlEglHooksMap,
                                sizeof(wlEglHooksMap)/sizeof(WlEglHook),
                                sizeof(WlEglHook),
                                hookCmp);
    if (hook) {
        return hook->func;
    }
    return NULL;
}

static EGLBoolean wlEglUnloadPlatformExport(void *data) {
    EGLBoolean res;

    res = wlEglDestroyAllDisplays((WlEglPlatformData *)data);
    wlEglDestroyPlatformData((WlEglPlatformData *)data);

    return res;
}

EGLBoolean loadEGLExternalPlatform(int major, int minor,
                                   const EGLExtDriver *driver,
                                   EGLExtPlatform *platform)
{
    if (!platform ||
        !EGL_EXTERNAL_PLATFORM_VERSION_CMP(major, minor,
            WAYLAND_EXTERNAL_VERSION_MAJOR, WAYLAND_EXTERNAL_VERSION_MINOR)) {
        return EGL_FALSE;
    }

    platform->version.major = WAYLAND_EXTERNAL_VERSION_MAJOR;
    platform->version.minor = WAYLAND_EXTERNAL_VERSION_MINOR;
    platform->version.micro = WAYLAND_EXTERNAL_VERSION_MICRO;

    platform->platform = EGL_PLATFORM_WAYLAND_EXT;

    platform->data = (void *)wlEglCreatePlatformData(major, minor, driver);
    if (platform->data == NULL) {
        return EGL_FALSE;
    }

    platform->exports.unloadEGLExternalPlatform = wlEglUnloadPlatformExport;

    platform->exports.getHookAddress       = wlEglGetHookAddressExport;
    platform->exports.isValidNativeDisplay = wlEglIsValidNativeDisplayExport;
    platform->exports.getPlatformDisplay   = wlEglGetPlatformDisplayExport;
    platform->exports.queryString          = wlEglQueryStringExport;
    platform->exports.getInternalHandle    = wlEglGetInternalHandleExport;

    return EGL_TRUE;
}
