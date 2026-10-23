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

# Windows takes the app icon from an icon resource in the .exe; see the .rc.
windows_res = []
if host_machine.system() == 'windows'
  windows_res = import('windows').compile_resources('monday-night-net.rc',
    depend_files : 'radio.ki6kvz.MondayNightNet.ico',
    include_directories : include_directories('.'))
endif

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

# So `meson devenv` runs the app without installing the schema.
schema_devenv = environment()
schema_devenv.prepend('GSETTINGS_SCHEMA_DIR', meson.current_build_dir())
meson.add_devenv(schema_devenv)

desktop_file = configure_file(input: 'radio.ki6kvz.MondayNightNet.desktop.in',
                              output: '@0@.desktop'.format(app_id),
                              configuration: resource_conf_data)
install_data(desktop_file, install_dir: get_option('prefix') / get_option('datadir') / 'applications')

# Renamed to app_id so the .Devel build's desktop file finds its icon too.
install_data('radio.ki6kvz.MondayNightNet.svg',
             install_dir: get_option('prefix') / get_option('datadir') / 'icons' / 'hicolor' / 'scalable' / 'apps',
             rename: '@0@.svg'.format(app_id))

metainfo_file = configure_file(input: 'radio.ki6kvz.MondayNightNet.metainfo.xml.in',
                               output: '@0@.metainfo.xml'.format(app_id),
                               configuration: resource_conf_data)
install_data(metainfo_file, install_dir: get_option('prefix') / get_option('datadir') / 'metainfo')

appstreamcli_prog = find_program('appstreamcli', required : false)
if appstreamcli_prog.found()
    test('validate-metainfo',
        appstreamcli_prog,
        args: ['validate', '--no-net', '--explain', metainfo_file],
        suite: 'validation',
    )
endif

gnome.post_install(glib_compile_schemas: true, gtk_update_icon_cache: true)
test('validate-schema',
  find_program('glib-compile-schemas'),
  args: ['--strict', '--dry-run', meson.current_build_dir()],
  suite: 'validation',
)
