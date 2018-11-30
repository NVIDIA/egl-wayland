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

#if HAS_MINCORE
#include <dlfcn.h> // Need dlsym() to load mincore() symbol dynamically.
#endif

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

#if HAS_MINCORE
EGLBoolean wlEglPointerIsDereferencable(void *p)
{
    /*
     * BSD and Solaris have slightly different prototypes for mincore, but
     * they should be compatible with this.  BSD uses:
     *
     *   (const void *, size_t, char *)
     *
     * And Solaris uses:
     *
     *   (caddr_t, size_t, char*)
     *
     * Which I believe are all ABI compatible with the Linux prototype used
     * below for MINCOREPROC.
     */
    typedef int (*MINCOREPROC)(void *, size_t, unsigned char *);
    static MINCOREPROC pMinCore = NULL;
    static EGLBoolean minCoreLoadAttempted = EGL_FALSE;
    uintptr_t addr = (uintptr_t) p;
    unsigned char unused;
    const long page_size = getpagesize();

    if (minCoreLoadAttempted == EGL_FALSE) {
        minCoreLoadAttempted = EGL_TRUE;

        /*
         * According to its manpage, mincore was introduced in Linux 2.3.99pre1
         * and glibc 2.2.  The minimum glibc our driver supports is 2.0, so this
         * mincore can not be linked in directly.  It does however seem
         * reasonable to assume that Wayland will not be run on glibc < 2.2.
         *
         * Attempt to load mincore from the currently available libraries.
         * mincore comes from libc, which the EGL driver depends on, so it
         * should always be loaded if our driver is running.
         */
        pMinCore = (MINCOREPROC)dlsym(NULL, "mincore");
    }

    /*
     * If the pointer can't be tested for safety, or is obviously unsafe,
     * assume it can't be dereferenced.
     */
    if (p == NULL || !pMinCore) {
        return EGL_FALSE;
    }

    /* align addr to page_size */
    addr &= ~(page_size - 1);

    /*
     * mincore() returns 0 on success, and -1 on failure.  The last parameter
     * is a vector of bytes with one entry for each page queried.  mincore
     * returns page residency information in the first bit of each byte in the
     * vector.
     *
     * Residency doesn't actually matter when determining whether a pointer is
     * dereferenceable, so the output vector can be ignored.  What matters is
     * whether mincore succeeds.  It will fail with ENOMEM if the range
     * [addr, addr + length) is not mapped into the process, so all that needs
     * to be checked there is whether the mincore call succeeds or not, as it
     * can only succeed on dereferenceable memory ranges.
     */
    return (pMinCore((void *) addr, page_size, &unused) >= 0);
}
#endif

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
