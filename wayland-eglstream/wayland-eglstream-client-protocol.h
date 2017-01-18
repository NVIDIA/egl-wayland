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

#ifndef STREAM_CLIENT_PROTOCOL_H
#define STREAM_CLIENT_PROTOCOL_H

#ifdef  __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include "wayland-client.h"

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

static inline void
wl_eglstream_set_user_data(struct wl_eglstream *wl_eglstream, void *user_data)
{
	wl_proxy_set_user_data((struct wl_proxy *) wl_eglstream, user_data);
}

static inline void *
wl_eglstream_get_user_data(struct wl_eglstream *wl_eglstream)
{
	return wl_proxy_get_user_data((struct wl_proxy *) wl_eglstream);
}

static inline void
wl_eglstream_destroy(struct wl_eglstream *wl_eglstream)
{
	wl_proxy_destroy((struct wl_proxy *) wl_eglstream);
}

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

struct wl_eglstream_display_listener {
	/**
	 * caps - Server capabilities event
	 * @caps: Capabilities mask
	 *
	 * The capabilities event is sent out at wl_eglstream_display
	 * binding time. It allows the server to advertise what features it
	 * supports so clients may know what is safe to be used.
	 */
	void (*caps)(void *data,
		     struct wl_eglstream_display *wl_eglstream_display,
		     int32_t caps);
	/**
	 * swapinterval_override - Server Swap interval override event
	 * @swapinterval: Server swap interval override value
	 * @stream: wl_buffer corresponding to an EGLStream
	 *
	 * The swapinterval_override event is sent out whenever a client
	 * requests a swapinterval setting through swap_interval() and
	 * there is an override in place that will make such request to be
	 * ignored. The swapinterval_override event will provide the
	 * override value so that the client is made aware of it.
	 */
	void (*swapinterval_override)(void *data,
				      struct wl_eglstream_display *wl_eglstream_display,
				      int32_t swapinterval,
				      struct wl_buffer *stream);
};

static inline int
wl_eglstream_display_add_listener(struct wl_eglstream_display *wl_eglstream_display,
				  const struct wl_eglstream_display_listener *listener, void *data)
{
	return wl_proxy_add_listener((struct wl_proxy *) wl_eglstream_display,
				     (void (**)(void)) listener, data);
}

#define WL_EGLSTREAM_DISPLAY_CREATE_STREAM	0
#define WL_EGLSTREAM_DISPLAY_SWAP_INTERVAL	1

static inline void
wl_eglstream_display_set_user_data(struct wl_eglstream_display *wl_eglstream_display, void *user_data)
{
	wl_proxy_set_user_data((struct wl_proxy *) wl_eglstream_display, user_data);
}

static inline void *
wl_eglstream_display_get_user_data(struct wl_eglstream_display *wl_eglstream_display)
{
	return wl_proxy_get_user_data((struct wl_proxy *) wl_eglstream_display);
}

static inline void
wl_eglstream_display_destroy(struct wl_eglstream_display *wl_eglstream_display)
{
	wl_proxy_destroy((struct wl_proxy *) wl_eglstream_display);
}

static inline struct wl_buffer *
wl_eglstream_display_create_stream(struct wl_eglstream_display *wl_eglstream_display, int32_t width, int32_t height, int32_t handle, int32_t type, struct wl_array *attribs)
{
	struct wl_proxy *id;

	id = wl_proxy_marshal_constructor((struct wl_proxy *) wl_eglstream_display,
			 WL_EGLSTREAM_DISPLAY_CREATE_STREAM, &wl_buffer_interface, NULL, width, height, handle, type, attribs);

	return (struct wl_buffer *) id;
}

static inline void
wl_eglstream_display_swap_interval(struct wl_eglstream_display *wl_eglstream_display, struct wl_buffer *stream, int32_t interval)
{
	wl_proxy_marshal((struct wl_proxy *) wl_eglstream_display,
			 WL_EGLSTREAM_DISPLAY_SWAP_INTERVAL, stream, interval);
}

#ifdef  __cplusplus
}
#endif

#endif
