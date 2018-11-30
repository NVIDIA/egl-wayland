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

#ifndef WAYLAND_EGL_EXT_H
#define WAYLAND_EGL_EXT_H

#ifndef EGL_WL_wayland_eglstream
#define EGL_WL_wayland_eglstream 1
#define EGL_WAYLAND_EGLSTREAM_WL              0x334B
#endif /* EGL_WL_wayland_eglstream */

#ifndef EGL_NV_stream_fifo_synchronous
#define EGL_NV_stream_fifo_synchronous 1
#define EGL_STREAM_FIFO_SYNCHRONOUS_NV               0x3336
#endif /* EGL_NV_stream_fifo_synchronous */

#ifndef EGL_NV_stream_flush
#define EGL_NV_stream_flush 1
typedef EGLBoolean (EGLAPIENTRYP PFNEGLSTREAMFLUSHNVPROC) (EGLDisplay dpy, EGLStreamKHR stream);
#ifdef EGL_EGLEXT_PROTOTYPES
EGLAPI EGLBoolean EGLAPIENTRY eglStreamFlushNV (EGLDisplay dpy, EGLStreamKHR stream);
#endif
#endif /*EGL_NV_stream_flush*/

/* Deprecated. Use EGL_KHR_stream_attrib */
#ifndef EGL_NV_stream_attrib
#define EGL_NV_stream_attrib 1
#ifdef EGL_EGLEXT_PROTOTYPES
EGLAPI EGLStreamKHR EGLAPIENTRY eglCreateStreamAttribNV(EGLDisplay dpy, const EGLAttrib *attrib_list);
#endif
typedef EGLStreamKHR (EGLAPIENTRYP PFNEGLCREATESTREAMATTRIBNVPROC) (EGLDisplay dpy, const EGLAttrib *attrib_list);
#endif /* EGL_NV_stream_attrib */

#ifndef EGL_KHR_display_reference
#define EGL_KHR_display_reference 1
#define EGL_TRACK_REFERENCES_KHR          0x3352
typedef EGLBoolean (EGLAPIENTRYP PFNEGLQUERYDISPLAYATTRIBKHRPROC) (EGLDisplay dpy, EGLint name, EGLAttrib *value);
#ifdef EGL_EGLEXT_PROTOTYPES
EGLAPI EGLBoolean EGLAPIENTRY eglQueryDisplayAttribKHR (EGLDisplay dpy, EGLint name, EGLAttrib *value);
#endif
#endif /* EGL_KHR_display_reference */

#endif
