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

#ifndef WAYLAND_EXTERNAL_EXPORTS_H
#define WAYLAND_EXTERNAL_EXPORTS_H

/*
 * <WAYLAND_EXTERNAL_VERSION_MAJOR>.<WAYLAND_EXTERNAL_VERSION_MINOR>.
 * <WAYLAND_EXTERNAL_VERSION_MICRO> defines the EGL external Wayland
 * implementation version.
 *
 * The includer of this file can override either WAYLAND_EXTERNAL_VERSION_MAJOR
 * or WAYLAND_EXTERNAL_VERSION_MINOR in order to build against a certain EGL
 * external API version.
 *
 *
 * How to update this version numbers:
 *
 *  - WAYLAND_EXTERNAL_VERSION_MAJOR must match the EGL external API major
 *    number this platform implements
 *
 *  - WAYLAND_EXTERNAL_VERSION_MINOR must match the EGL external API minor
 *    number this platform implements
 *
 *  - If the platform implementation is changed in any way, increase
 *    WAYLAND_EXTERNAL_VERSION_MICRO by 1
 */
#if !defined(WAYLAND_EXTERNAL_VERSION_MAJOR)
 #define WAYLAND_EXTERNAL_VERSION_MAJOR                      1
 #if !defined(WAYLAND_EXTERNAL_VERSION_MINOR)
  #define WAYLAND_EXTERNAL_VERSION_MINOR                     1
 #endif
#elif !defined(WAYLAND_EXTERNAL_VERSION_MINOR)
 #define WAYLAND_EXTERNAL_VERSION_MINOR                      0
#endif

#define WAYLAND_EXTERNAL_VERSION_MICRO                       20


#define EGL_EXTERNAL_PLATFORM_VERSION_MAJOR WAYLAND_EXTERNAL_VERSION_MAJOR
#define EGL_EXTERNAL_PLATFORM_VERSION_MINOR WAYLAND_EXTERNAL_VERSION_MINOR
#include <eglexternalplatform.h>

#include <wayland-util.h>

WL_EXPORT
EGLBoolean loadEGLExternalPlatform(int major, int minor,
                                   const EGLExtDriver *driver,
                                   EGLExtPlatform *platform);

#endif
