project('wayland-eglstream', 'c',
        version : '1.1.20',
        default_options : [
          'buildtype=debugoptimized',
          'c_std=gnu99',
          'warning_level=1',
        ],
        license : 'MIT',
        meson_version : '>= 0.50'
)

cc = meson.get_compiler('c')

wayland_eglstream_version = meson.project_version()
ver_arr = wayland_eglstream_version.split('.')

wayland_eglstream_major_version = ver_arr[0]
wayland_eglstream_minor_version = ver_arr[1]
wayland_eglstream_micro_version = ver_arr[2]

egl = dependency('egl', version : ['>=1.5', '<2'])
egl_headers = egl.partial_dependency(includes : true, compile_args : true)
eglexternalplatform = dependency('eglexternalplatform', version : ['>=1.1', '<2'])
wayland_server = dependency('wayland-server')
wayland_client = dependency('wayland-client')
wayland_egl_backend = dependency('wayland-egl-backend', version : ['>=3'])
threads = dependency('threads')

wl_scanner = dependency('wayland-scanner', native: true)
prog_scanner = find_program(wl_scanner.get_pkgconfig_variable('wayland_scanner'))

inc = include_directories(
    'include',
    'wayland-egl',
)

pkgconf = configuration_data()
pkgconf.set('prefix',      get_option('prefix'))
pkgconf.set('exec_prefix', '${prefix}')
pkgconf.set('libdir',      '${exec_prefix}/@0@'.format(get_option('libdir')))
pkgconf.set('includedir',  '${prefix}/@0@'.format(get_option('includedir')))
pkgconf.set('datadir',     '${datarootdir}')
pkgconf.set('datarootdir', '${prefix}/@0@'.format(get_option('datadir')))

pkgconf.set('PACKAGE',                           meson.project_name())
pkgconf.set('WAYLAND_EXTERNAL_VERSION',          meson.project_version())
pkgconf.set('EGL_EXTERNAL_PLATFORM_MIN_VERSION', '@0@.@1@'.format(wayland_eglstream_major_version, wayland_eglstream_minor_version))
pkgconf.set('EGL_EXTERNAL_PLATFORM_MAX_VERSION', wayland_eglstream_major_version.to_int() + 1)

configure_file(
    input : 'wayland-eglstream.pc.in',
    output : '@BASENAME@',
    configuration : pkgconf,
    install : true,
    install_dir : join_paths(get_option('libdir'), 'pkgconfig')
)
configure_file(
    input : 'wayland-eglstream-protocols.pc.in',
    output : '@BASENAME@',
    configuration : pkgconf,
    install : true,
    install_dir : join_paths(get_option('datadir'), 'pkgconfig')
)

subdir('wayland-eglstream')
subdir('wayland-drm')
subdir('src')
