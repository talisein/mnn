gnome = import('gnome')

# Verify json
jq_prog = find_program('jq', required : false)
if jq_prog.found()
    test('default-net json validation',
        jq_prog,
        args: ['-e', '.', 'default-net.json'],
        suite: 'validation',
        workdir: meson.current_source_dir()
    )
endif

res = gnome.compile_resources('monday-night-net-resources',
                              'monday-night-net.gresource.xml',
                              install_header : false,
                              export : false,
                              c_name : 'monday_night_net_resources')

# GSettings

schemas_dir = get_option('prefix') / get_option('datadir') / 'glib-2.0' / 'schemas'
settings_schemas = [ 'net.talinet.MondayNightNet.gschema.xml' ]
install_data(settings_schemas, install_dir: schemas_dir)
gnome.compile_schemas(depend_files: files(settings_schemas))
gnome.post_install(glib_compile_schemas: true)
test('validate-schema',
  find_program('glib-compile-schemas'),
  args: ['--strict', '--dry-run', meson.current_source_dir()],
  suite: 'validation',
)
