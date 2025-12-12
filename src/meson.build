conf_data = configuration_data()
conf_data.set10('has_backtrace', has_backtrace)
conf_data.set10('has_stacktrace', has_stacktrace)

conf = configure_file(input: 'config.hpp.in',
                      output: 'config.hpp',
                      configuration: conf_data)


libsrc = ['net_definition.cpp', 'mnn.cpp', 'mnn_application_window.cpp', 'mnn_application.cpp']
libmnn = static_library('mnn', libsrc, res, peel_gtk, conf,
                        dependencies: deps)

libmnn_dep = declare_dependency(link_with: libmnn,
                                dependencies: deps,
                                include_directories: include_directories('.'),
                                sources: conf)

src = ['main.cpp']
exe = executable('monday-night-net', src, res, peel_gtk,
                 dependencies: libmnn_dep,
                 install: true)
