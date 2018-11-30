if not cc.has_function('dlsym')
  libdl = cc.find_library('dl')
else
  libdl = []
endif

add_project_arguments('-Wall', language : 'c')
add_project_arguments('-Werror', language : 'c')
add_project_arguments('-fvisibility=hidden', language : 'c')
add_project_link_arguments('-Wl,-Bsymbolic', language : 'c')

if cc.has_argument('-Wpedantic')
        add_project_arguments('-Wno-pedantic', language : 'c')
endif

src = [
    'wayland-thread.c',
    'wayland-egldisplay.c',
    'wayland-eglstream.c',
    'wayland-eglstream-server.c',
    'wayland-eglsurface.c',
    'wayland-eglswap.c',
    'wayland-eglutils.c',
    'wayland-eglhandle.c',
    'wayland-external-exports.c',

    wayland_eglstream_protocol_c,
    wayland_eglstream_client_protocol_h,
    wayland_eglstream_server_protocol_h,
    wayland_eglstream_controller_protocol_c,
    wayland_eglstream_controller_client_protocol_h,
]

egl_wayland = library('nvidia-egl-wayland',
    src,
    dependencies : [
        eglexternalplatform,
        wayland_server,
        wayland_client,
        wayland_egl_backend,
        threads,
        libdl,
    ],
    include_directories : inc,
    version : meson.project_version(),
    install : true,
)
