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

#include <stdlib.h>
#include <stdint.h>
#include "wayland-util.h"

extern const struct wl_interface wl_buffer_interface;

static const struct wl_interface *types[] = {
	NULL,
	&wl_buffer_interface,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	&wl_buffer_interface,
	NULL,
	NULL,
	&wl_buffer_interface,
};

WL_EXPORT const struct wl_interface wl_eglstream_interface = {
	"wl_eglstream", 1,
	0, NULL,
	0, NULL,
};

static const struct wl_message wl_eglstream_display_requests[] = {
	{ "create_stream", "niihia", types + 1 },
	{ "swap_interval", "oi", types + 7 },
};

static const struct wl_message wl_eglstream_display_events[] = {
	{ "caps", "i", types + 0 },
	{ "swapinterval_override", "io", types + 9 },
};

WL_EXPORT const struct wl_interface wl_eglstream_display_interface = {
	"wl_eglstream_display", 1,
	2, wl_eglstream_display_requests,
	2, wl_eglstream_display_events,
};

