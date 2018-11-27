generated_private_protocols = [
    'wayland-eglstream',
]

generated_public_protocols = [
    'wayland-eglstream-controller',
]

foreach proto : generated_private_protocols + generated_public_protocols
    foreach output_type: ['client-header', 'server-header', 'private-code']
        if output_type == 'client-header'
            output_file = '@0@-client-protocol.h'.format(proto)
        elif output_type == 'server-header'
            output_file = '@0@-server-protocol.h'.format(proto)
        else
            output_file = '@0@-protocol.c'.format(proto)
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
            input: '@0@.xml'.format(proto),
            output: output_file,
        )

        set_variable(var_name, target)
    endforeach
endforeach

install_data(
    'wayland-eglstream.xml',
    'wayland-eglstream-controller.xml',
    install_dir : join_paths(get_option('datadir'), meson.project_name())
)
