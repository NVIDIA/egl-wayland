/*
 * Copyright (c) 2016, NVIDIA CORPORATION. All rights reserved.
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
#include <pthread.h>
#include <stdlib.h>
#include <assert.h>

static pthread_mutex_t wlMutex;
static pthread_once_t  wlMutexOnceControl = PTHREAD_ONCE_INIT;
static int             wlMutexInitialized = 0;

static pthread_key_t   wlTLSKey;
static pthread_once_t  wlTLSKeyOnceControl = PTHREAD_ONCE_INIT;
static int             wlTLSKeyInitialized = 0;

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

static void destroy_tls_key(void *data)
{
    WlThread     *wlThread = data;
    WlEventQueue *iter     = NULL;
    WlEventQueue *tmp      = NULL;

    if (wlThread) {
        /* Invalidate and destroy all queues */
        wl_list_for_each_safe(iter, tmp, &wlThread->evtQueueList, threadLink) {
            if (iter->queue != NULL) {
                wl_event_queue_destroy(iter->queue);
                wl_list_remove(&iter->dpyLink);
            }
            wl_list_remove(&iter->threadLink);
            free(iter);
        }

        free(wlThread);
    }
}

static void create_tls_key(void)
{
    /* Create a pthread storage key to be used to set and retrieave TLS data */
    if (pthread_key_create(&wlTLSKey, destroy_tls_key) == 0) {
        wlTLSKeyInitialized = 1;
    }
}

WlThread* wlGetThread(void)
{
    WlThread *wlThread = NULL;

    if (pthread_once(&wlTLSKeyOnceControl, create_tls_key)) {
        assert(!"pthread once failed");
        return NULL;
    }

    if (!wlTLSKeyInitialized) {
        assert(!"failed to create TLS key");
        return NULL;
    }

    wlThread = pthread_getspecific(wlTLSKey);
    if (wlThread == NULL) {
        wlThread = calloc(1, sizeof(WlThread));
        if (wlThread == NULL) {
            return NULL;
        }

        if (pthread_setspecific(wlTLSKey, wlThread) != 0) {
            assert(!"failed to set TLS data");
            free(wlThread);
            return NULL;
        }

        wl_list_init(&wlThread->evtQueueList);
    }

    return wlThread;
}
