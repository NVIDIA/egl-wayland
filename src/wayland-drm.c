/*
 * Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.
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
#include <unistd.h>

#include <wayland-server.h>
#include "wayland-eglstream-server.h"
#include "wayland-drm.h"
#include "wayland-eglutils.h"
#include "wayland-drm-server-protocol.h"

static void
authenticate(struct wl_client *client,
             struct wl_resource *resource, uint32_t id)
{
    (void)client;
    (void)id;

    /*
     * This implementation will only ever report render node files, and
     * authentication is not supported nor required for render node devices.
     */
    wl_resource_post_error(resource,
                           WL_DRM_ERROR_AUTHENTICATE_FAIL,
                           "authenticate failed");
}

static void
create_buffer(struct wl_client *client, struct wl_resource *resource,
              uint32_t id, uint32_t name, int32_t width, int32_t height,
              uint32_t stride, uint32_t format)
{
    (void)client;
    (void)id;
    (void)name;
    (void)width;
    (void)height;
    (void)stride;
    (void)format;

    wl_resource_post_error(resource,
                           WL_DRM_ERROR_INVALID_FORMAT,
                           "invalid format");
}

static void
create_planar_buffer(struct wl_client *client, struct wl_resource *resource,
                     uint32_t id, uint32_t name, int32_t width, int32_t height,
                     uint32_t format,
                     int32_t offset0, int32_t stride0,
                     int32_t offset1, int32_t stride1,
                     int32_t offset2, int32_t stride2)
{
    (void)client;
    (void)id;
    (void)name;
    (void)width;
    (void)height;
    (void)format;
    (void)offset0;
    (void)stride0;
    (void)offset1;
    (void)stride1;
    (void)offset2;
    (void)stride2;

    wl_resource_post_error(resource,
                           WL_DRM_ERROR_INVALID_FORMAT,
                           "invalid format");
}

static void
create_prime_buffer(struct wl_client *client, struct wl_resource *resource,
                    uint32_t id, int fd, int32_t width, int32_t height,
                    uint32_t format,
                    int32_t offset0, int32_t stride0,
                    int32_t offset1, int32_t stride1,
                    int32_t offset2, int32_t stride2)
{
    (void)client;
    (void)id;
    (void)fd;
    (void)width;
    (void)height;
    (void)format;
    (void)offset0;
    (void)stride0;
    (void)offset1;
    (void)stride1;
    (void)offset2;
    (void)stride2;

    wl_resource_post_error(resource,
                           WL_DRM_ERROR_INVALID_FORMAT,
                           "invalid format");
    close(fd);
}

static const struct wl_drm_interface interface = {
    authenticate,
    create_buffer,
    create_planar_buffer,
    create_prime_buffer,
};

static void
bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
    struct wl_eglstream_display *wlStreamDpy = data;
    struct wl_resource *resource = wl_resource_create(client, &wl_drm_interface,
                                                      version > 2 ? 2 : version,
                                                      id);

    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }

    wl_resource_set_implementation(resource, &interface, data, NULL);
    wl_resource_post_event(resource, WL_DRM_DEVICE, wlStreamDpy->drm->device_name);

    /*
     * Don't send any format events. This implementation doesn't support
     * creating Wayland buffers of any format.
     */

    /*
     * Don't report any capabilities beyond the baseline, as currently all
     * capabilities are only relevant when buffer creation is supported.
     */
    if (version >= 2)
        wl_resource_post_event(resource, WL_DRM_CAPABILITIES, 0);
}

const char *
wl_drm_get_dev_name(const WlEglPlatformData *data,
                    EGLDisplay dpy)
{
    EGLDeviceEXT egl_dev;
    const char *dev_exts;

    if (!data->egl.queryDisplayAttrib(dpy, EGL_DEVICE_EXT,
                                      (EGLAttribKHR*)&egl_dev)) {
        return NULL;
    }

    dev_exts = data->egl.queryDeviceString(egl_dev, EGL_EXTENSIONS);

    if (!dev_exts) {
        return NULL;
    }

    if (!wlEglFindExtension("EGL_EXT_device_drm_render_node", dev_exts)) {
        return NULL;
    }

    return data->egl.queryDeviceString(egl_dev, EGL_DRM_RENDER_NODE_FILE_EXT);
}

EGLBoolean
wl_drm_display_bind(struct wl_display *display,
                    struct wl_eglstream_display *wlStreamDpy,
                    const char *dev_name)
{
    if (!dev_name) {
        return EGL_FALSE;
    }

    wlStreamDpy->drm = calloc(1, sizeof *wlStreamDpy->drm);

    if (!wlStreamDpy->drm) {
        return EGL_FALSE;
    }

    wlStreamDpy->drm->device_name = dev_name;
    wlStreamDpy->drm->global = wl_global_create(display, &wl_drm_interface, 2,
                                                wlStreamDpy, bind);

    return EGL_TRUE;
}

void
wl_drm_display_unbind(struct wl_eglstream_display *wlStreamDpy)
{
    if (wlStreamDpy->drm) {
        wl_global_destroy(wlStreamDpy->drm->global);
        free(wlStreamDpy->drm);
        wlStreamDpy->drm = NULL;
    }
}
