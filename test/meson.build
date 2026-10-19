test_env = environment()
test_env.set('GOM_MODULES_DIR',
             meson.global_build_root() / 'subprojects' / 'gom' / 'lib' / 'sqlite')

ut_args = ['-r', 'junit', '-o']
station_test =  executable('station_test', 'station.cpp',
                           dependencies: [test_deps, libmnn_dep])
test('station', station_test, args: [ut_args])


prop_watcher_test =  executable('prop_watcher_test', 'test_prop_watcher.cpp',
                                dependencies: [test_deps, libmnn_dep])
test('prop_watcher', prop_watcher_test, args: [ut_args])

position_lookup_test = executable('position_lookup_test', 'test_position_lookup.cpp',
                                  dependencies: [test_deps, libmnn_dep])
test('position_lookup', position_lookup_test, args: [ut_args], env: test_env)

nominatim_test = executable('nominatim_test', 'test_nominatim.cpp',
                            dependencies: [test_deps, libmnn_dep])
test('nominatim', nominatim_test, args: [ut_args], env: test_env)

coordinates_test = executable('coordinates_test', 'test_coordinates.cpp',
                              dependencies: [test_deps, libmnn_dep])
test('coordinates', coordinates_test, args: [ut_args])
