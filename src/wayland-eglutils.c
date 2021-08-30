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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "wayland-eglutils.h"
#include "wayland-thread.h"
#include "wayland-eglhandle.h"
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>

EGLBoolean wlEglFindExtension(const char *extension, const char *extensions)
{
    const char *start;
    const char *where, *terminator;

    start = extensions;
    for (;;) {
        where = strstr(start, extension);
        if (!where) {
            break;
        }
        terminator = where + strlen(extension);
        if (where == start || *(where - 1) == ' ') {
            if (*terminator == ' ' || *terminator == '\0') {
                return EGL_TRUE;
            }
        }
        start = terminator;
    }

    return EGL_FALSE;
}

EGLBoolean wlEglMemoryIsReadable(const void *p, size_t len)
{
    int fds[2], result = -1;
    if (pipe(fds) == -1) {
        return EGL_FALSE;
    }

    if (fcntl(fds[1], F_SETFL, O_NONBLOCK) == -1) {
        goto done;
    }

    /* write will fail with EFAULT if the provided buffer is outside
     * our accessible address space. */
    result = write(fds[1], p, len);
    assert(result != -1 || errno == EFAULT);

done:
    close(fds[0]);
    close(fds[1]);
    return result != -1;
}

EGLBoolean wlEglCheckInterfaceType(struct wl_object *obj, const char *ifname)
{
    /* The first member of a wl_object is a pointer to its wl_interface, */
    struct wl_interface *interface = *(void **)obj;

    /* Check if the memory for the wl_interface struct, and the
     * interface name, are safe to read. */
    int len = strlen(ifname);
    if (!wlEglMemoryIsReadable(interface, sizeof (*interface)) ||
        !wlEglMemoryIsReadable(interface->name, len + 1)) {
        return EGL_FALSE;
    }


    return !strcmp(interface->name, ifname);
}

void wlEglSetErrorCallback(WlEglPlatformData *data,
                           EGLint error,
                           const char *file,
                           int line)
{
    if (data && data->callbacks.setError) {
        const char *defaultMsg = "Wayland external platform error";

        if (file != NULL) {
            char msg[256];
            if (snprintf(msg, 256, "%s:%d: %s", file, line, defaultMsg) > 0) {
                data->callbacks.setError(error, EGL_DEBUG_MSG_ERROR_KHR, msg);
                return;
            }
        }
        data->callbacks.setError(error, EGL_DEBUG_MSG_ERROR_KHR, defaultMsg);
    }
}
