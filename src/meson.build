

libsrc = ['net_definition.cpp']
libmnn = static_library('mnn', libsrc, res, peel_gtk,
                        dependencies: deps)

src = ['main.cpp']
exe = executable('monday-night-net', src, res, peel_gtk,
                 dependencies: deps,
                 link_with: [libmnn],
                 install: true)
