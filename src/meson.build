if not cc.has_function('dlsym')
  libdl = cc.find_library('dl')
else
  libdl = []
endif

add_project_arguments('-pthread', language : 'c')

add_project_link_arguments(
    '-pthread',
    '-Wl,-Bsymbolic',
    language : 'c'
)

foreach arg : [
               'unused-parameter',
               'pedantic',
               'deprecated-declarations',
              ]
        if cc.has_argument('-W' + arg)
                add_project_arguments('-Wno-' + arg, language : 'c')
        endif
endforeach

src = [
    'wayland-api-lock.c',
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
        libdl,
    ],
    include_directories : [inc, top_srcdir_inc],
    version : meson.project_version(),
    install : true,
)
