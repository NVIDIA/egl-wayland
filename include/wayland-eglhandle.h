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

#ifndef WAYLAND_EGLHANDLE_H
#define WAYLAND_EGLHANDLE_H

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include "wayland-external-exports.h"
#include "wayland-egl-ext.h"
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Define function pointers for EGL core functions
 */
typedef const char* (*PWLEGLFNQUERYSTRINGCOREPROC)              (EGLDisplay dpy, EGLint name);
typedef EGLContext  (*PWLEGLFNGETCURRENTCONTEXTCOREPROC)        (void);
typedef EGLSurface  (*PWLEGLFNGETCURRENTSURFACECOREPROC)        (EGLint readdraw);
typedef EGLBoolean  (*PWLEGLFNRELEASETHREADCOREPROC)            (void);
typedef EGLint      (*PWLEGLFNGETERRORCOREPROC)                 (void);
typedef void*       (*PWLEGLFNGETPROCADDRESSCOREPROC)           (const char *name);
typedef EGLBoolean  (*PWLEGLFNINITIALIZECOREPROC)               (EGLDisplay dpy, EGLint *major, EGLint *minor);
typedef EGLBoolean  (*PWLEGLFNTERMINATECOREPROC)                (EGLDisplay dpy);
typedef EGLBoolean  (*PWLEGLFNCHOOSECONFIGCOREPROC)             (EGLDisplay dpy, const EGLint *attrib_list, EGLConfig *configs, EGLint config_size, EGLint *num_config);
typedef EGLBoolean  (*PWLEGLFNGETCONFIGATTRIBCOREPROC)          (EGLDisplay dpy, EGLConfig config, EGLint attribute, EGLint *value);
typedef EGLSurface  (*PWLEGLFNCREATEPBUFFERSURFACECOREPROC)     (EGLDisplay dpy, EGLConfig config, const EGLint *attrib_list);
typedef EGLBoolean  (*PWLEGLFNDESTROYSURFACECOREPROC)           (EGLDisplay dpy, EGLSurface surface);
typedef EGLBoolean  (*PWLEGLFNMAKECURRENTCOREPROC)              (EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx);
typedef EGLBoolean  (*PWLEGLFNSWAPBUFFERSCOREPROC)              (EGLDisplay dpy, EGLSurface surface);
typedef EGLBoolean  (*PWLEGLFNSWAPBUFFERSWITHDAMAGEKHRPROC)     (EGLDisplay dpy, EGLSurface surface, EGLint *rects, EGLint n_rects);
typedef EGLBoolean  (*PWLEGLFNSWAPINTERVALCOREPROC)             (EGLDisplay dpy, EGLint interval);


/*
 * WlEglPlatformData structure
 *
 * Keeps all EGL driver-specific methods provided by a specific EGL
 * implementation that are required by the Wayland external platform
 * implementation to manage resources associated with a specific backing
 * EGLDisplay.
 */
typedef struct WlEglPlatformDataRec {
    /* Application-facing callbacks fetched from the EGL driver */
    struct {
        int                                         major;
        int                                         minor;

        PWLEGLFNQUERYSTRINGCOREPROC                 queryString;
        PFNEGLQUERYDEVICESEXTPROC                   queryDevices;

        PFNEGLGETPLATFORMDISPLAYEXTPROC             getPlatformDisplay;
        PWLEGLFNINITIALIZECOREPROC                  initialize;
        PWLEGLFNTERMINATECOREPROC                   terminate;
        PWLEGLFNCHOOSECONFIGCOREPROC                chooseConfig;
        PWLEGLFNGETCONFIGATTRIBCOREPROC             getConfigAttrib;
        PFNEGLQUERYSURFACEPROC                      querySurface;

        PWLEGLFNGETCURRENTCONTEXTCOREPROC           getCurrentContext;
        PWLEGLFNGETCURRENTSURFACECOREPROC           getCurrentSurface;
        PWLEGLFNMAKECURRENTCOREPROC                 makeCurrent;

        PFNEGLCREATESTREAMKHRPROC                   createStream;
        PFNEGLCREATESTREAMFROMFILEDESCRIPTORKHRPROC createStreamFromFD;
        PFNEGLCREATESTREAMATTRIBNVPROC              createStreamAttrib;
        PFNEGLGETSTREAMFILEDESCRIPTORKHRPROC        getStreamFileDescriptor;
        PFNEGLCREATESTREAMPRODUCERSURFACEKHRPROC    createStreamProducerSurface;
        PWLEGLFNCREATEPBUFFERSURFACECOREPROC        createPbufferSurface;
        PFNEGLDESTROYSTREAMKHRPROC                  destroyStream;
        PWLEGLFNDESTROYSURFACECOREPROC              destroySurface;

        PWLEGLFNSWAPBUFFERSCOREPROC                 swapBuffers;
        PWLEGLFNSWAPBUFFERSWITHDAMAGEKHRPROC        swapBuffersWithDamage;
        PWLEGLFNSWAPINTERVALCOREPROC                swapInterval;

        PWLEGLFNGETERRORCOREPROC                    getError;
        PWLEGLFNRELEASETHREADCOREPROC               releaseThread;

        PFNEGLQUERYDISPLAYATTRIBEXTPROC             queryDisplayAttrib;
        PFNEGLQUERYDEVICESTRINGEXTPROC              queryDeviceString;

        /* Used for fifo_synchronous support */
        PFNEGLQUERYSTREAMKHRPROC                    queryStream;
        PFNEGLQUERYSTREAMU64KHRPROC                 queryStreamu64;
        PFNEGLCREATESTREAMSYNCNVPROC                createStreamSync;
        PFNEGLCLIENTWAITSYNCKHRPROC                 clientWaitSync;
        PFNEGLSIGNALSYNCKHRPROC                     signalSync;
        PFNEGLDESTROYSYNCKHRPROC                    destroySync;
        PFNEGLCREATESYNCKHRPROC                     createSync;
        PFNEGLSTREAMFLUSHNVPROC                     streamFlush;
        PFNEGLDUPNATIVEFENCEFDANDROIDPROC           dupNativeFenceFD;

        /* Used for dma-buf surfaces */
        PFNEGLSTREAMIMAGECONSUMERCONNECTNVPROC      streamImageConsumerConnect;
        PFNEGLSTREAMACQUIREIMAGENVPROC              streamAcquireImage;
        PFNEGLSTREAMRELEASEIMAGENVPROC              streamReleaseImage;
        PFNEGLQUERYSTREAMCONSUMEREVENTNVPROC        queryStreamConsumerEvent;
        PFNEGLEXPORTDMABUFIMAGEMESAPROC             exportDMABUFImage;
        PFNEGLEXPORTDMABUFIMAGEQUERYMESAPROC        exportDMABUFImageQuery;
        PFNEGLCREATEIMAGEKHRPROC                    createImage;
        PFNEGLDESTROYIMAGEKHRPROC                   destroyImage;
    } egl;

    /* Non-application-facing callbacks provided by the EGL driver */
    struct {
        PEGLEXTFNSETERROR           setError;
        PEGLEXTFNSTREAMSWAPINTERVAL streamSwapInterval;
    } callbacks;

    /* True if the driver supports the EGL_KHR_display_reference extension. */
    EGLBoolean supportsDisplayReference;

    /* A linked list of WlEglDeviceDpy structs. */
    struct wl_list deviceDpyList;

    /* pthread key for TLS */
    pthread_key_t tlsKey;
} WlEglPlatformData;


/*
 * wlEglCreatePlatformData()
 *
 * Creates a new platform data structure and fills it out with all the required
 * application-facing EGL methods provided by <driver>.
 *
 * <apiMajor>.<apiMinor> correspond to the EGL External Platform interface
 * version supported by the driver.
 *
 * Returns a pointer to the newly created structure upon success; otherwise,
 * returns NULL.
 */
WlEglPlatformData*
wlEglCreatePlatformData(int apiMajor, int apiMinor, const EGLExtDriver *driver);

/*
 * wlEglDestroyPlatformData()
 *
 * Destroys the given platform data, previously created with
 * wlEglCreatePlatformData().
 */
void wlEglDestroyPlatformData(WlEglPlatformData *data);

void* wlEglGetInternalHandleExport(EGLDisplay dpy, EGLenum type, void *handle);

#ifdef __cplusplus
}
#endif

#endif
