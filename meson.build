project('wayland-eglstream', 'c',
	version : '1.0.2',
	default_options : [
          'buildtype=debugoptimized',
          'c_std=gnu99',
          'warning_level=1',
        ],
	license : 'MIT',
        meson_version : '>= 0.43'
)

cc = meson.get_compiler('c')

wayland_eglstream_version = meson.project_version()
ver_arr = wayland_eglstream_version.split('.')

wayland_eglstream_major_version = ver_arr[0]
wayland_eglstream_minor_version = ver_arr[1]
wayland_eglstream_micro_version = ver_arr[2]

eglexternalplatform = dependency('eglexternalplatform', version : ['>=1.0', '<2'])
wayland_server = dependency('wayland-server')
wayland_client = dependency('wayland-client')

wl_scanner = find_program('wayland-scanner')

inc = include_directories(
    'include',
    'wayland-egl',
    'wayland-eglstream',
)

top_srcdir_inc = include_directories('.')

cdata = configuration_data()
cdata.set_quoted('PACKAGE',            meson.project_name(),                                            description : 'Name of package')
cdata.set_quoted('PACKAGE_BUGREPORT',  'mvicomoya@nvidia.com',                                          description : 'Define to the address where bug reports for this package should be sent.')
cdata.set_quoted('PACKAGE_NAME',       meson.project_name(),                                            description : 'Define to the full name of this package.')
cdata.set_quoted('PACKAGE_STRING',     '@0@ @1@'.format(meson.project_name(), meson.project_version()), description : 'Define to the full name and version of this package.')
cdata.set_quoted('PACKAGE_TARNAME',     meson.project_name(),                                           description : 'Define to the one symbol short name of this package.')
cdata.set_quoted('PACKAGE_URL',        '',                                                              description : 'Define to the home page for this package.')
cdata.set_quoted('PACKAGE_VERSION',    meson.project_version(),                                         description : 'Define to the version of this package.')

check_headers = [
    ['arpa/inet.h',  'HAVE_ARPA_INET_H'],
    ['stddef.h',     'HAVE_STDDEF_H'],
    ['stdint.h',     'HAVE_STDINT_H'],
    ['stdlib.h',     'HAVE_STDLIB_H'],
    ['string.h',     'HAVE_STRING_H'],
    ['sys/socket.h', 'HAVE_SYS_SOCKET_H'],
    ['unistd.h',     'HAVE_UNISTD_H'],
]

foreach h : check_headers
  if cc.has_header(h.get(0))
    cdata.set10(h.get(1), true)
  endif
endforeach

check_functions = [
    ['getpagesize', 'HAVE_GETPAGESIZE'],
    ['inet_ntoa',   'HAVE_INET_NTOA'],
    ['memset',      'HAVE_MEMSET'],
    ['socket',      'HAVE_SOCKET'],
    ['strcasecmp',  'HAVE_STRCASECMP'],
    ['strstr',      'HAVE_STRSTR'],
]

foreach f : check_functions
  if cc.has_function(f.get(0))
    cdata.set10(f.get(1), true)
  endif
endforeach

configure_file(
    output : 'config.h',
    configuration : cdata
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
pkgconf.set('EGL_EXTERNAL_PLATFORM_MAX_VERSION', [wayland_eglstream_major_version.to_int() + 1])

configure_file(input : 'wayland-eglstream.pc.in',
               output : 'wayland-eglstream.pc',
               configuration : pkgconf,
               install : true,
               install_dir : join_paths(get_option('datadir'), 'pkgconfig')
)

configure_file(input : 'wayland-eglstream-protocols.pc.in',
               output : 'wayland-eglstream-protocols.pc',
               configuration : pkgconf,
               install : true,
               install_dir : join_paths(get_option('datadir'), 'pkgconfig')
)

subdir('wayland-eglstream')
subdir('src')
