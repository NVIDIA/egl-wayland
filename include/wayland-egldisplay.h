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

#include <sys/types.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <wayland-client.h>
#include <pthread.h>
#include "wayland-external-exports.h"
#include "wayland-eglhandle.h"
#include "wayland-egldevice.h"

#ifdef __cplusplus
extern "C" {
#endif

/* This define represents the version of the wl_eglstream_controller interface
   when the attach_eglstream_consumer_attrib() request was first available" */
#define WL_EGLSTREAM_CONTROLLER_ATTACH_EGLSTREAM_CONSUMER_ATTRIB_SINCE 2

/*
 * Our representation of a dmabuf format. It has a drm_fourcc.h code
 * and a collection of modifiers that are supported.
 */
typedef struct WlEglDmaBufFormatRec {
    uint32_t format;
    uint32_t numModifiers;
    uint64_t *modifiers;
} WlEglDmaBufFormat;

/*
 * This is a helper struct for a collection of dmabuf formats. We have
 * a couple areas of the code that want to manage sets of formats and
 * this allows us to share code.
 */
typedef struct WlEglDmaBufFormatSetRec {
    uint32_t numFormats;
    WlEglDmaBufFormat *dmaBufFormats;
} WlEglDmaBufFormatSet;

/*
 * Container for all formats supported by a device. A "tranche" is
 * really just a set of formats and modifiers supported by a device
 * with a certain set of flags (really just a scanout flag).
 */
typedef struct WlEglDmaBufTrancheRec {
    dev_t drmDev;
    int supportsScanout;
    WlEglDmaBufFormatSet formatSet;
} WlEglDmaBufTranche;

/*
 * This is one item in the format table that the compositor sent
 * us.
 */
typedef struct WlEglDmaBufFormatTableEntryRec{
    uint32_t format;
    uint32_t pad;
    uint64_t modifier;
} WlEglDmaBufFormatTableEntry;

/*
 * In linux_dmabuf_feedback.format_table the compositor will advertise all
 * (format, modifier) pairs that it supports importing buffers with. We
 * the client mmap this format table and refer to it during the tranche
 * events to construct WlEglDmaBufFormatSets that the compositor
 * supports.
 */
typedef struct WlEglDmaBufFormatTableRec {
    int len;
    /* This is mmapped from the fd given to us by the compositor */
    WlEglDmaBufFormatTableEntry *entry;
} WlEglDmaBufFormatTable;

/*
 * A dmabuf feedback object. This will record all tranches sent by the
 * compositor. It can be used either for per-surface feedback or for
 * the default feedback for any surface.
 */
typedef struct WlEglDmaBufFeedbackRec {
    struct zwp_linux_dmabuf_feedback_v1 *wlDmaBufFeedback;
    int numTranches;
    WlEglDmaBufTranche *tranches;
    WlEglDmaBufFormatTable formatTable;
    dev_t mainDev;
    /*
     * This will be filled in during wl events and copied to
     * dev_formats on dmabuf_feedback.tranche_done
     */
    WlEglDmaBufTranche tmpTranche;
    int feedbackDone;
    /*
     * This will be set to true if the compositor notified us of new
     * modifiers but we haven't reallocated our surface yet.
     */
    int unprocessedFeedback;
} WlEglDmaBufFeedback;

typedef struct WlEglDisplayRec {
    WlEglDeviceDpy *devDpy;

    /* Supports EGL_ANDROID_native_fence_sync */
    int supports_native_fence_sync;
    /* Underlying driver version is recent enough for explicit sync */
    int supports_explicit_sync;

    EGLBoolean         ownNativeDpy;
    struct wl_display *nativeDpy;

    struct wl_registry             *wlRegistry;
    struct wl_eglstream_display    *wlStreamDpy;
    struct wl_eglstream_controller *wlStreamCtl;
    struct zwp_linux_dmabuf_v1     *wlDmaBuf;
    struct wp_linux_drm_syncobj_manager_v1 *wlDrmSyncobj;
    unsigned int                    wlStreamCtlVer;
    struct wp_presentation         *wpPresentation;
    struct wl_event_queue          *wlEventQueue;
    struct {
        unsigned int stream_fd     : 1;
        unsigned int stream_inet   : 1;
        unsigned int stream_socket : 1;
    } caps;

    WlEglPlatformData *data;

    /* DRM device in use */
    int drmFd;

    EGLBoolean useInitRefCount;
    EGLDeviceEXT requestedDevice;

    /**
     * The number of times that eglTerminate has to be called before the
     * display is termianted.
     *
     * If \c useInitRefCount is true, then this is incremented each time
     * eglInitialize is called, and decremented each time eglTerminate is
     * called.
     *
     * If \c useInitRefCount is false, then this value is capped at 1.
     *
     * In all cases, the display is initialized if (initCount > 0).
     */
    unsigned int initCount;

    pthread_mutex_t mutex;

    int refCount;

    struct wl_list wlEglSurfaceList;

    struct wl_list link;

    /* The formats given to us by the linux_dmabuf.modifiers event */
    WlEglDmaBufFormatSet formatSet;

    /* The linux_dmabuf protocol version in use. Will be >= 3 */
    unsigned int dmaBufProtocolVersion;

    WlEglDmaBufFeedback defaultFeedback;

    EGLBoolean primeRenderOffload;

    char *extensionString;
} WlEglDisplay;

typedef struct WlEventQueueRec {
    WlEglDisplay          *display;
    struct wl_event_queue *queue;
    int                    refCount;

    struct wl_list dpyLink;
    struct wl_list dangLink;
    struct wl_list threadLink;
} WlEventQueue;

int WlEglRegisterFeedback(WlEglDmaBufFeedback *feedback);
void wlEglDestroyFeedback(WlEglDmaBufFeedback *feedback);
EGLBoolean wlEglIsValidNativeDisplayExport(void *data, void *nativeDpy);
EGLBoolean wlEglBindDisplaysHook(void *data, EGLDisplay dpy, void *nativeDpy);
EGLBoolean wlEglUnbindDisplaysHook(EGLDisplay dpy, void *nativeDpy);
EGLDisplay wlEglGetPlatformDisplayExport(void *data,
                                         EGLenum platform,
                                         void *nativeDpy,
                                         const EGLAttrib *attribs);
EGLBoolean wlEglInitializeHook(EGLDisplay dpy, EGLint *major, EGLint *minor);
EGLBoolean wlEglTerminateHook(EGLDisplay dpy);
WlEglDisplay *wlEglAcquireDisplay(EGLDisplay dpy);
void wlEglReleaseDisplay(WlEglDisplay *display);

EGLBoolean wlEglChooseConfigHook(EGLDisplay dpy,
                                 EGLint const * attribs,
                                 EGLConfig * configs,
                                 EGLint configSize,
                                 EGLint * numConfig);
EGLBoolean wlEglGetConfigAttribHook(EGLDisplay dpy,
                                    EGLConfig config,
                                    EGLint attribute,
                                    EGLint * value);

EGLBoolean wlEglQueryDisplayAttribHook(EGLDisplay dpy,
                                       EGLint name,
                                       EGLAttrib *value);
const char* wlEglQueryStringHook(EGLDisplay dpy, EGLint name);


EGLBoolean wlEglIsWaylandDisplay(void *nativeDpy);
EGLBoolean wlEglIsWlEglDisplay(WlEglDisplay *display);

EGLBoolean wlEglDestroyAllDisplays(WlEglPlatformData *data);

const char* wlEglQueryStringExport(void *data,
                                   EGLDisplay dpy,
                                   EGLExtPlatformString name);

#ifdef __cplusplus
}
#endif

#endif
