project('monday-night-net', ['c', 'cpp'],
        license: 'GPLv3+',
        version: '0.0.1',
        meson_version: '>=1.4.0',
        default_options: ['cpp_std=c++26',
                          'buildtype=debugoptimized'])

cpp_compiler = meson.get_compiler('cpp')
gtk = dependency('gtk4')
libadwaita = dependency('libadwaita-1', version: '>= 1.5')
libshumate = dependency('shumate-1.0')
libsoup = dependency('libsoup-3.0')
libjson = dependency('nlohmann_json')
libmagic_enum = dependency('magic_enum')
libcorefoundation = dependency('appleframeworks', modules: ['CoreFoundation'], required: false)

libstdcppexp = declare_dependency()
has_stacktrace = false
if cpp_compiler.links('#include <stacktrace>\nint main() { std::stacktrace::current(); return 0; }', name: 'std::stacktrace::current()', args: '-std=@0@'.format(get_option('cpp_std')), dependencies: libstdcppexp)
  has_stacktrace = true
else
  if cpp_compiler.get_id() == 'gcc'
    explib = cpp_compiler.find_library('stdc++exp', required: false)
    if explib.found()
      libstdcppexp = declare_dependency(dependencies: explib)
      if cpp_compiler.links('#include <stacktrace>\nint main() { std::stacktrace::current(); return 0; }', name: 'std::stacktrace::current()', args: '-std=@0@'.format(get_option('cpp_std')), dependencies: libstdcppexp)
        has_stacktrace = true
      endif
    endif
  endif
endif

libexecinfo_backtrace = declare_dependency()
has_backtrace = false
if cpp_compiler.has_header_symbol('execinfo.h', 'backtrace') and cpp_compiler.has_header_symbol('execinfo.h', 'backtrace_symbols_fd') and cpp_compiler.has_header_symbol('csignal', 'std::signal') and cpp_compiler.has_header_symbol('unistd.h', 'pipe')
  has_backtrace = true
  if cpp_compiler.has_link_argument('-rdynamic')
    libexecinfo_backtrace = declare_dependency(link_args: '-rdynamic')
  endif
endif

libboostut = dependency('ut',
                        default_options: {'wrap_mode': 'forcefallback'},
                        fallback: ['boostut', 'boostut_dep'])

# Take peel_dep straight from the subproject rather than dependency('peel').
# peel's meson.build calls meson.override_dependency('peel', ...), and meson
# errors if a dependency('peel') resolution (e.g. a ~/.local CMake install) and
# that override both register 'peel'. find_program('peel-gen') below still
# picks up the subproject's override_find_program.
peel_subp = subproject('peel')
peel = peel_subp.get_variable('peel_dep')

# gom master needs libdex >= 1.2; Fedora ships 1.1, so this falls back to the
# libdex.wrap subproject. Introspection must be on: gom's gir includes Dex-1,
# and peel-gen consumes both girs from the build tree.
libdex = dependency('libdex-1', version: '>= 1.2',
                    default_options: {'introspection': 'enabled',
                                      'default_library': 'shared'})

gom_subp = subproject('gom',
                      default_options: {'introspection': 'enabled',
                                        'default_library': 'shared',
                                        'tests': false,
                                        'vapi': false,
                                        'pygobject': false,
                                        'docs': false,
                                        'demos': false,
                                        'postgresql-backend': 'disabled'})
libgom = gom_subp.get_variable('libgom_dep')
libgom_gir = gom_subp.get_variable('libgom_gir')

peel_gen = find_program('peel-gen')

# Gom-2.gir and Dex-1.gir are generated inside the subproject build dirs;
# peel-gen searches GI_GIR_PATH before the system gir-1.0 directories.
subproject_gir_path = ':'.join([
  meson.global_build_root() / 'subprojects' / 'gom' / 'lib',
  meson.global_build_root() / 'subprojects' / 'libdex' / 'src',
])

peel_gtk = custom_target('peel-codegen',
  command: [
    peel_gen,
    '--recursive',
    '--out-dir', '@OUTDIR@',
    'Gtk', '4.0',
    'Adw', '1',
    'Shumate', '1.0',
    'Soup', '3.0',
    'Dex', '1',
    'Gom', '2',
  ],
  env: {'GI_GIR_PATH': subproject_gir_path},
  depends: libgom_gir,
  output: 'peel',
                        )

peel_gtk_dep = declare_dependency(sources: peel_gtk)

# gom loads its sqlite backend as a GModule; point it at the build tree.
devenv = environment()
devenv.set('GOM_MODULES_DIR',
           meson.global_build_root() / 'subprojects' / 'gom' / 'lib' / 'sqlite')
meson.add_devenv(devenv)

deps = [gtk, libadwaita, libshumate, libsoup, libjson, libmagic_enum, peel, libdex, libgom, libstdcppexp, libexecinfo_backtrace, libcorefoundation, peel_gtk_dep]
test_deps = [libboostut, deps]

add_project_arguments('-D_GNU_SOURCE', language: ['c', 'cpp'])

if get_option('debug')
  add_project_arguments('-DG_ENABLE_DEBUG', language: ['c', 'cpp'])
endif

app_id = 'radio.ki6kvz.MondayNightNet'
if get_option('development')
  app_id = '@0@.Devel'.format(app_id)
endif

subdir('po')
subdir('res')
subdir('src')
subdir('test')
