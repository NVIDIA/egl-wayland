foreach output_type: ['client-header', 'server-header', 'public-code']
    if output_type == 'client-header'
        output_file = 'wayland-drm-client-protocol.h'
    elif output_type == 'server-header'
        output_file = 'wayland-drm-server-protocol.h'
    else
        output_file = 'wayland-drm-protocol.c'

        if wl_scanner.version().version_compare('< 1.14.91')
                output_type = 'code'
            elif generated_public_protocols.contains(proto)
                output_type = 'public-code'
            endif
    endif

    var_name = output_file.underscorify()
    target = custom_target(
        '@0@'.format(output_file),
        command: [prog_scanner, output_type, '@INPUT@', '@OUTPUT@'],
        input: 'wayland-drm.xml',
        output: output_file,
    )

    set_variable(var_name, target)
endforeach

install_data(
    'wayland-drm.xml',
    install_dir : join_paths(get_option('datadir'), meson.project_name())
)
