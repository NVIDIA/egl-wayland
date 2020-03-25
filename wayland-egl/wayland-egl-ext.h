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

#ifndef EGL_WL_bind_wayland_display
#define EGL_WL_bind_wayland_display 1
#define PFNEGLBINDWAYLANDDISPLAYWL PFNEGLBINDWAYLANDDISPLAYWLPROC
#define PFNEGLUNBINDWAYLANDDISPLAYWL PFNEGLUNBINDWAYLANDDISPLAYWLPROC
#define PFNEGLQUERYWAYLANDBUFFERWL PFNEGLQUERYWAYLANDBUFFERWLPROC
struct wl_display;
struct wl_resource;
#define EGL_WAYLAND_BUFFER_WL             0x31D5
#define EGL_WAYLAND_PLANE_WL              0x31D6
#define EGL_TEXTURE_Y_U_V_WL              0x31D7
#define EGL_TEXTURE_Y_UV_WL               0x31D8
#define EGL_TEXTURE_Y_XUXV_WL             0x31D9
#define EGL_TEXTURE_EXTERNAL_WL           0x31DA
#define EGL_WAYLAND_Y_INVERTED_WL         0x31DB
typedef EGLBoolean (EGLAPIENTRYP PFNEGLBINDWAYLANDDISPLAYWLPROC) (EGLDisplay dpy, struct wl_display *display);
typedef EGLBoolean (EGLAPIENTRYP PFNEGLUNBINDWAYLANDDISPLAYWLPROC) (EGLDisplay dpy, struct wl_display *display);
typedef EGLBoolean (EGLAPIENTRYP PFNEGLQUERYWAYLANDBUFFERWLPROC) (EGLDisplay dpy, struct wl_resource *buffer, EGLint attribute, EGLint *value);
#ifdef EGL_EGLEXT_PROTOTYPES
EGLAPI EGLBoolean EGLAPIENTRY eglBindWaylandDisplayWL (EGLDisplay dpy, struct wl_display *display);
EGLAPI EGLBoolean EGLAPIENTRY eglUnbindWaylandDisplayWL (EGLDisplay dpy, struct wl_display *display);
EGLAPI EGLBoolean EGLAPIENTRY eglQueryWaylandBufferWL (EGLDisplay dpy, struct wl_resource *buffer, EGLint attribute, EGLint *value);
#endif
#endif /* EGL_WL_bind_wayland_display */

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

#ifndef EGL_NV_stream_origin
#define EGL_NV_stream_origin 1
#define EGL_STREAM_FRAME_ORIGIN_X_NV         0x3366
#define EGL_STREAM_FRAME_ORIGIN_Y_NV         0x3367
#define EGL_STREAM_FRAME_MAJOR_AXIS_NV       0x3368
#define EGL_CONSUMER_AUTO_ORIENTATION_NV     0x3369
#define EGL_PRODUCER_AUTO_ORIENTATION_NV     0x336A
#define EGL_LEFT_NV                          0x336B
#define EGL_RIGHT_NV                         0x336C
#define EGL_TOP_NV                           0x336D
#define EGL_BOTTOM_NV                        0x336E
#define EGL_X_AXIS_NV                        0x336F
#define EGL_Y_AXIS_NV                        0x3370
#endif /* EGL_NV_stream_origin */

#endif
