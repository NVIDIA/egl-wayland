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

#ifndef WL_EGLSTREAM_CONTROLLER_CLIENT_PROTOCOL_H
#define WL_EGLSTREAM_CONTROLLER_CLIENT_PROTOCOL_H

#ifdef  __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include "wayland-client.h"

struct wl_client;
struct wl_resource;

struct wl_buffer;
struct wl_eglstream_controller;
struct wl_surface;

extern const struct wl_interface wl_eglstream_controller_interface;

#define WL_EGLSTREAM_CONTROLLER_ATTACH_EGLSTREAM_CONSUMER	0

static inline void
wl_eglstream_controller_set_user_data(struct wl_eglstream_controller *wl_eglstream_controller, void *user_data)
{
	wl_proxy_set_user_data((struct wl_proxy *) wl_eglstream_controller, user_data);
}

static inline void *
wl_eglstream_controller_get_user_data(struct wl_eglstream_controller *wl_eglstream_controller)
{
	return wl_proxy_get_user_data((struct wl_proxy *) wl_eglstream_controller);
}

static inline void
wl_eglstream_controller_destroy(struct wl_eglstream_controller *wl_eglstream_controller)
{
	wl_proxy_destroy((struct wl_proxy *) wl_eglstream_controller);
}

static inline void
wl_eglstream_controller_attach_eglstream_consumer(struct wl_eglstream_controller *wl_eglstream_controller, struct wl_surface *wl_surface, struct wl_buffer *wl_resource)
{
	wl_proxy_marshal((struct wl_proxy *) wl_eglstream_controller,
			 WL_EGLSTREAM_CONTROLLER_ATTACH_EGLSTREAM_CONSUMER, wl_surface, wl_resource);
}

#ifdef  __cplusplus
}
#endif

#endif
