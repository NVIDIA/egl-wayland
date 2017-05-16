/* 
 * Copyright (c) 2017, NVIDIA CORPORATION. All rights reserved.
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

#ifndef WL_EGLSTREAM_CONTROLLER_SERVER_PROTOCOL_H
#define WL_EGLSTREAM_CONTROLLER_SERVER_PROTOCOL_H

#ifdef  __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include "wayland-server.h"

struct wl_client;
struct wl_resource;

struct wl_buffer;
struct wl_eglstream_controller;
struct wl_surface;

extern const struct wl_interface wl_eglstream_controller_interface;

struct wl_eglstream_controller_interface {
	/**
	 * attach_eglstream_consumer - Create server stream and attach
	 *	consumer
	 * @wl_surface: wl_surface corresponds to the client surface
	 *	associated with newly created eglstream
	 * @wl_resource: wl_resource corresponding to an EGLStream
	 *
	 * Creates the corresponding server side EGLStream from the given
	 * wl_buffer and attaches a consumer to it.
	 */
	void (*attach_eglstream_consumer)(struct wl_client *client,
					  struct wl_resource *resource,
					  struct wl_resource *wl_surface,
					  struct wl_resource *wl_resource);
};


#ifdef  __cplusplus
}
#endif

#endif
