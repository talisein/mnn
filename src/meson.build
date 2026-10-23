
app_id = 'radio.ki6kvz.MondayNightNet'
if get_option('development')
  app_id = '@0@.Devel'.format(app_id)
endif

conf_data = configuration_data()
conf_data.merge_from(resource_conf_data)
conf_data.set10('has_backtrace', has_backtrace)
conf_data.set10('has_stacktrace', has_stacktrace)
conf_data.set10('has_core_foundation', libcorefoundation.found())
conf_data.set('project_version', meson.project_version())
conf_data.set_quoted('locale_dir', join_paths(get_option('prefix'), get_option('datadir'), 'locale'))

conf = configure_file(input: 'config.hpp.in',
                      output: 'config.hpp',
                      configuration: conf_data)

libsrc = ['net_definition.cpp', 'mnn.cpp', 'mnn_error.cpp',
          'mnn_application_window.cpp', 'mnn_application.cpp', 'station.cpp',
          'mnn_callsign_list_view_cell.cpp', 'mnn_button_list_view_cell.cpp',
          'mnn_station_column_view.cpp', 'mnn_station_map_view.cpp',
          'list_property_watcher.cpp',
          'http_client.cpp', 'database.cpp', 'entities.cpp', 'position_lookup.cpp',
          'cooldown.cpp', 'nominatim.cpp', 'position.cpp']
libmnn = static_library('mnn', libsrc, res, conf,
                        dependencies: deps)

libmnn_dep = declare_dependency(link_with: libmnn,
                                dependencies: deps,
                                include_directories: include_directories('.'),
                                sources: conf)

src = ['main.cpp']
# A GUI program on Windows, so starting it opens no console window. With
# MSVC the windows subsystem expects WinMain(); the entry point keeps main().
exe_link_args = []
if host_machine.system() == 'windows' and cpp_compiler.get_argument_syntax() == 'msvc'
  exe_link_args += '/ENTRY:mainCRTStartup'
endif
exe = executable('monday-night-net', src, res, windows_res,
                 dependencies: libmnn_dep,
                 win_subsystem: 'windows',
                 link_args: exe_link_args,
                 install: true)
