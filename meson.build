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
                        default_options: {'wrap_mode': 'force'},
                        fallback: ['boostut', 'boostut_dep'])

peel = dependency('peel')



peel_gen = find_program('peel-gen')

peel_gtk = custom_target('peel-codegen',
  command: [
    peel_gen,
    '--recursive',
    '--out-dir', '@OUTDIR@',
    'Gtk', '4.0',
    'Adw', '1',
    'Shumate', '1.0',
  ],
  output: 'peel',
                        )

peel_gtk_dep = declare_dependency(sources: peel_gtk)

deps = [gtk, libadwaita, libshumate, libjson, libmagic_enum, peel, libstdcppexp, libexecinfo_backtrace, libcorefoundation, peel_gtk_dep]
test_deps = [libboostut, deps]

add_project_arguments('-D_GNU_SOURCE', language: ['c', 'cpp'])

if get_option('debug')
  add_project_arguments('-DG_ENABLE_DEBUG', language: ['c', 'cpp'])
endif

subdir('po')
subdir('res')
subdir('src')
subdir('test')
