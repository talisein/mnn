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


resource_basepath = ''.join('/', app_id.replace('.','/'))
schema_path = ''.join('/', app_id.replace('.','/'), '/State/')
schema_id = ''.join(app_id, '.State')
resource_conf_data = configuration_data()
resource_conf_data.set('resource_basepath', resource_basepath)
resource_conf_data.set('app_id', app_id)
resource_conf_data.set('schema_path', schema_path)
resource_conf_data.set('schema_id', schema_id)
gresource_xml = configure_file(input: 'monday-night-net.gresource.xml.in',
                               output: 'monday-night-net.gresource.xml',
                               configuration: resource_conf_data)

res = gnome.compile_resources('mnn_resources',
                              gresource_xml,
                              install_header : false,
                              export : false,
                              c_name : 'mnn_resources')

# GSettings

schemas_dir = get_option('prefix') / get_option('datadir') / 'glib-2.0' / 'schemas'
settings_schema = configure_file(input: 'radio.ki6kvz.MondayNightNet.gschema.xml.in',
                                 output: '@0@.gschema.xml'.format(app_id),
                                 configuration: resource_conf_data)

install_data(settings_schema, install_dir: schemas_dir)

gschemas_compiled = custom_target('gschemas.compiled',
                                  output: 'gschemas.compiled',
                                  command: ['glib-compile-schemas', meson.current_build_dir()],
                                  depend_files: settings_schema,
                                  build_by_default: true,
                                  install: false,
                                 )

gnome.post_install(glib_compile_schemas: true)
test('validate-schema',
  find_program('glib-compile-schemas'),
  args: ['--strict', '--dry-run', meson.current_build_dir()],
  suite: 'validation',
)
