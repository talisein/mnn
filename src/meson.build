
app_id = 'radio.ki6kvz.MondayNightNet'
if get_option('development')
  app_id = '@0@.Devel'.format(app_id)
endif

conf_data = configuration_data()
conf_data.set10('has_backtrace', has_backtrace)
conf_data.set10('has_stacktrace', has_stacktrace)
conf_data.set10('has_core_foundation', libcorefoundation.found())
conf_data.set('project_version', meson.project_version())
conf_data.set('app_id', app_id)
conf_data.set_quoted('locale_dir', join_paths(get_option('prefix'), get_option('datadir'), 'locale'))

conf = configure_file(input: 'config.hpp.in',
                      output: 'config.hpp',
                      configuration: conf_data)

libsrc = ['net_definition.cpp', 'mnn.cpp', 'mnn_error.cpp', 'mnn_application_window.cpp', 'mnn_application.cpp', 'station.cpp', 'mnn_callsign_list_view_cell.cpp']
libmnn = static_library('mnn', libsrc, res, conf,
                        dependencies: deps)

libmnn_dep = declare_dependency(link_with: libmnn,
                                dependencies: deps,
                                include_directories: include_directories('.'),
                                sources: conf)

src = ['main.cpp']
exe = executable('monday-night-net', src, res,
                 dependencies: libmnn_dep,
                 install: true)
