/*
 * Copyright (c) 2016-2019, NVIDIA CORPORATION. All rights reserved.
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

/* To include PTHREAD_MUTEX_ERRORCHECK */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "wayland-thread.h"
#include "wayland-egldisplay.h"
#include <stdlib.h>
#include <assert.h>

#if defined(__QNX__)
#define WL_EGL_ATTRIBUTE_DESTRUCTOR
#define WL_EGL_ATEXIT(func) atexit(func)
#else
#define WL_EGL_ATTRIBUTE_DESTRUCTOR __attribute__((destructor))
#define WL_EGL_ATEXIT(func) 0
#endif

static pthread_mutex_t wlMutex;
static pthread_once_t  wlMutexOnceControl = PTHREAD_ONCE_INIT;
static int             wlMutexInitialized = 0;

static void wlExternalApiInitializeLock(void)
{
    pthread_mutexattr_t attr;

    if (pthread_mutexattr_init(&attr)) {
        assert(!"failed to initialize pthread attribute mutex");
        return;
    }

    if (pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK)) {
        assert(!"failed to set pthread attribute mutex errorcheck");
        goto fail;
    }

    if (pthread_mutex_init(&wlMutex, &attr)) {
        assert(!"failed to initialize pthread mutex");
        goto fail;
    }

    wlMutexInitialized = 1;

fail:
    if (pthread_mutexattr_destroy(&attr)) {
        assert(!"failed to destroy pthread attribute mutex");
    }
}

void wlExternalApiDestroyLock(void)
{
    if (!wlMutexInitialized || pthread_mutex_destroy(&wlMutex)) {
        assert(!"failed to destroy pthread mutex");
    }
}

int wlExternalApiLock(void)
{
    if (pthread_once(&wlMutexOnceControl, wlExternalApiInitializeLock)) {
        assert(!"pthread once failed");
        return -1;
    }

    if (!wlMutexInitialized || pthread_mutex_lock(&wlMutex)) {
        assert(!"failed to lock pthread mutex");
        return -1;
    }

    return 0;
}

int wlExternalApiUnlock(void)
{
    if (!wlMutexInitialized || pthread_mutex_unlock(&wlMutex)) {
        assert(!"failed to unlock pthread mutex");
        return -1;
    }

    return 0;
}

bool wlEglInitializeMutex(pthread_mutex_t *mutex)
{
    pthread_mutexattr_t attr;
    bool ret = true;

    if (pthread_mutexattr_init(&attr)) {
        return false;
    }

    if (pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK)) {
        ret = false;
        goto done;
    }

    if (pthread_mutex_init(mutex, &attr)) {
        ret = false;
        goto done;
    }

done:
    pthread_mutexattr_destroy(&attr);
    return ret;
}

void wlEglMutexDestroy(pthread_mutex_t *mutex)
{
    pthread_mutex_destroy(mutex);
}
