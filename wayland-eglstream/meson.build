wayland_eglstream_protocol_c = custom_target(
  'wayland-eglstream-protocol.c',
  input : 'wayland-eglstream.xml',
  output : 'wayland-eglstream-protocol.c',
  command : [wl_scanner, 'code', '@INPUT@', '@OUTPUT@'],
)

wayland_eglstream_client_protocol_h = custom_target(
  'wayland-eglstream-client-protocol.h',
  input : 'wayland-eglstream.xml',
  output : 'wayland-eglstream-client-protocol.h',
  command : [wl_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
)

wayland_eglstream_server_protocol_h = custom_target(
  'wayland-eglstream-server-protocol.h',
  input : 'wayland-eglstream.xml',
  output : 'wayland-eglstream-server-protocol.h',
  command : [wl_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
)

wayland_eglstream_controller_protocol_c = custom_target(
  'wayland-eglstream-controller-protocol.c',
  input : 'wayland-eglstream-controller.xml',
  output : 'wayland-eglstream-controller-protocol.c',
  command : [wl_scanner, 'code', '@INPUT@', '@OUTPUT@'],
)

wayland_eglstream_controller_client_protocol_h = custom_target(
  'wayland-eglstream-controller-client-protocol.h',
  input : 'wayland-eglstream-controller.xml',
  output : 'wayland-eglstream-controller-client-protocol.h',
  command : [wl_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
)

install_data(
    'wayland-eglstream.xml',
    'wayland-eglstream-controller.xml',
    install_dir : join_paths(get_option('datadir'), meson.project_name())
)
