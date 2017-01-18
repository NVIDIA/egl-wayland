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

#ifndef STREAM_SERVER_PROTOCOL_H
#define STREAM_SERVER_PROTOCOL_H

#ifdef  __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include "wayland-server.h"

struct wl_client;
struct wl_resource;

struct wl_buffer;
struct wl_eglstream;
struct wl_eglstream_display;

extern const struct wl_interface wl_eglstream_interface;
extern const struct wl_interface wl_eglstream_display_interface;

#ifndef WL_EGLSTREAM_ERROR_ENUM
#define WL_EGLSTREAM_ERROR_ENUM
enum wl_eglstream_error {
	WL_EGLSTREAM_ERROR_BAD_ALLOC = 0,
	WL_EGLSTREAM_ERROR_BAD_HANDLE = 1,
	WL_EGLSTREAM_ERROR_BAD_ATTRIBS = 2,
	WL_EGLSTREAM_ERROR_BAD_ADDRESS = 3,
};
#endif /* WL_EGLSTREAM_ERROR_ENUM */

#ifndef WL_EGLSTREAM_HANDLE_TYPE_ENUM
#define WL_EGLSTREAM_HANDLE_TYPE_ENUM
/**
 * wl_eglstream_handle_type - Stream handle type
 * @WL_EGLSTREAM_HANDLE_TYPE_FD: File descriptor
 * @WL_EGLSTREAM_HANDLE_TYPE_INET: Inet connection
 * @WL_EGLSTREAM_HANDLE_TYPE_SOCKET: Unix socket
 *
 * - fd: The given handle represents a file descriptor, and the EGLStream
 * connection must be done as described in EGL_KHR_stream_cross_process_fd
 *
 * - inet: The EGLStream connection must be done using an inet address and
 * port as described in EGL_NV_stream_socket. The given handle can be
 * ignored, but both inet address and port must be given as attributes.
 *
 * - socket: The given handle represents a unix socket, and the EGLStream
 * connection must be done as described in EGL_NV_stream_socket.
 */
enum wl_eglstream_handle_type {
	WL_EGLSTREAM_HANDLE_TYPE_FD = 0,
	WL_EGLSTREAM_HANDLE_TYPE_INET = 1,
	WL_EGLSTREAM_HANDLE_TYPE_SOCKET = 2,
};
#endif /* WL_EGLSTREAM_HANDLE_TYPE_ENUM */

#ifndef WL_EGLSTREAM_ATTRIB_ENUM
#define WL_EGLSTREAM_ATTRIB_ENUM
/**
 * wl_eglstream_attrib - Stream creation attributes
 * @WL_EGLSTREAM_ATTRIB_INET_ADDR: Inet IPv4 address
 * @WL_EGLSTREAM_ATTRIB_INET_PORT: IP port
 *
 * - inet_addr: The given attribute encodes an IPv4 address of a client
 * socket. Both IPv4 address and port must be set at the same time.
 *
 * - inet_port: The given attribute encodes a port of a client socket. Both
 * IPv4 address and port must be set at the same time.
 */
enum wl_eglstream_attrib {
	WL_EGLSTREAM_ATTRIB_INET_ADDR = 0,
	WL_EGLSTREAM_ATTRIB_INET_PORT = 1,
};
#endif /* WL_EGLSTREAM_ATTRIB_ENUM */


#ifndef WL_EGLSTREAM_DISPLAY_CAP_ENUM
#define WL_EGLSTREAM_DISPLAY_CAP_ENUM
/**
 * wl_eglstream_display_cap - wl_eglstream_display capability codes
 * @WL_EGLSTREAM_DISPLAY_CAP_STREAM_FD: Stream connection with FD
 * @WL_EGLSTREAM_DISPLAY_CAP_STREAM_INET: Stream inet connection
 * @WL_EGLSTREAM_DISPLAY_CAP_STREAM_SOCKET: Stream unix connection
 *
 * This enum values should be used as bit masks.
 *
 * - stream_fd: The server supports EGLStream connections as described in
 * EGL_KHR_stream_cross_process_fd
 *
 * - stream_inet: The server supports EGLStream inet connections as
 * described in EGL_NV_stream_socket.
 *
 * - stream_socket: The server supports EGLStream unix socket connections
 * as described in EGL_NV_stream_socket.
 */
enum wl_eglstream_display_cap {
	WL_EGLSTREAM_DISPLAY_CAP_STREAM_FD = 1,
	WL_EGLSTREAM_DISPLAY_CAP_STREAM_INET = 2,
	WL_EGLSTREAM_DISPLAY_CAP_STREAM_SOCKET = 4,
};
#endif /* WL_EGLSTREAM_DISPLAY_CAP_ENUM */

struct wl_eglstream_display_interface {
	/**
	 * create_stream - List of attributes with extra connection data
	 * @id: New ID
	 * @width: Stream framebuffer width
	 * @height: Stream framebuffer height
	 * @handle: Handle for the stream creation
	 * @type: Handle type
	 * @attribs: Stream extra connection attribs
	 *
	 * It contains key-value pairs compatible with intptr_t type. A
	 * key must be one of wl_eglstream_display_attrib enumeration
	 * values. What a value represents is attribute-specific.
	 */
	void (*create_stream)(struct wl_client *client,
			      struct wl_resource *resource,
			      uint32_t id,
			      int32_t width,
			      int32_t height,
			      int32_t handle,
			      int32_t type,
			      struct wl_array *attribs);
	/**
	 * swap_interval - change the swap interval of an EGLStream
	 *	consumer
	 * @stream: wl_buffer corresponding to an EGLStream
	 * @interval: new swap interval
	 *
	 * Set the swap interval for the consumer of the given EGLStream.
	 * The swap interval is silently clamped to the valid range on the
	 * server side.
	 */
	void (*swap_interval)(struct wl_client *client,
			      struct wl_resource *resource,
			      struct wl_resource *stream,
			      int32_t interval);
};

#define WL_EGLSTREAM_DISPLAY_CAPS	0
#define WL_EGLSTREAM_DISPLAY_SWAPINTERVAL_OVERRIDE	1

#define WL_EGLSTREAM_DISPLAY_CAPS_SINCE_VERSION	1
#define WL_EGLSTREAM_DISPLAY_SWAPINTERVAL_OVERRIDE_SINCE_VERSION	1

static inline void
wl_eglstream_display_send_caps(struct wl_resource *resource_, int32_t caps)
{
	wl_resource_post_event(resource_, WL_EGLSTREAM_DISPLAY_CAPS, caps);
}

static inline void
wl_eglstream_display_send_swapinterval_override(struct wl_resource *resource_, int32_t swapinterval, struct wl_resource *stream)
{
	wl_resource_post_event(resource_, WL_EGLSTREAM_DISPLAY_SWAPINTERVAL_OVERRIDE, swapinterval, stream);
}

#ifdef  __cplusplus
}
#endif

#endif
