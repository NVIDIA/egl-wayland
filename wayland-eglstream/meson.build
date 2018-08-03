generated_protocols = [
    'wayland-eglstream',
    'wayland-eglstream-controller',
]

foreach proto : generated_protocols
    proto_name = proto
    xml_path = '@0@.xml'.format(proto_name)

    foreach output_type: ['client-header', 'server-header', 'private-code']
        if output_type == 'client-header'
            output_file = '@0@-client-protocol.h'.format(proto_name)
        elif output_type == 'server-header'
            output_file = '@0@-server-protocol.h'.format(proto_name)
        else
            output_file = '@0@-protocol.c'.format(proto_name)
            if wl_scanner.version().version_compare('< 1.14.91')
                output_type = 'code'
            endif
        endif

        var_name = output_file.underscorify()
        target = custom_target(
            '@0@'.format(output_file),
            command: [prog_scanner, output_type, '@INPUT@', '@OUTPUT@'],
            input: xml_path,
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
