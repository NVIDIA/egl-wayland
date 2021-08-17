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

#ifndef WAYLAND_EGLUTILS_H
#define WAYLAND_EGLUTILS_H

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include "wayland-external-exports.h"
#include "wayland-eglhandle.h"

#ifdef NDEBUG
#define wlEglSetError(data, err) \
        wlEglSetErrorCallback(data, err, 0, 0)
#else
#define wlEglSetError(data, err) \
        wlEglSetErrorCallback(data, err, __FILE__, __LINE__)
#endif

#ifndef WL_LIST_INITIALIZER
#define WL_LIST_INITIALIZER(head) { .prev = (head), .next = (head) }
#endif

#ifndef WL_LIST_INIT
#define WL_LIST_INIT(head)                                      \
    do { (head)->prev = (head)->next = (head); } while (0);
#endif

#if defined(__QNX__)
#define HAS_MINCORE 0
#else
#define HAS_MINCORE 1
#endif

#ifdef __cplusplus
extern "C" {
#endif

EGLBoolean wlEglFindExtension(const char *extension, const char *extensions);
#if HAS_MINCORE
EGLBoolean wlEglPointerIsDereferencable(void *p);
EGLBoolean wlEglCheckInterfaceType(struct wl_object *obj, const char *ifname);
#ifndef WL_CHECK_INTERFACE_TYPE
#define WL_CHECK_INTERFACE_TYPE(obj, iftype, ifname)             \
    (*(void **)(obj) == &iftype ||                               \
    wlEglCheckInterfaceType((struct wl_object *)(obj), ifname))
#endif
#endif
void wlEglSetErrorCallback(WlEglPlatformData *data,
                           EGLint err,
                           const char *file,
                           int line);
#ifdef __cplusplus
}
#endif

#endif
