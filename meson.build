project('monday-night-net', ['c', 'cpp'],
        license: 'GPLv3+',
        version: '0.0.1',
        meson_version: '>=1.4.0',
        default_options: ['cpp_std=c++26',
                          'buildtype=release',
                          'optimization=g'])

gtk = dependency('gtk4')
libadwaita = dependency('libadwaita-1', version: '>= 1.5')
libshumate = dependency('shumate-1.0')
libjson = dependency('nlohmann_json')
libmagic_enum = dependency('magic_enum')
libboostut = dependency('ut',
                        default_options: {'wrap_mode': 'force'},
                        fallback: ['boostut', 'boostut_dep'])

peel = dependency('peel')


deps = [gtk, libadwaita, libshumate, libjson, libmagic_enum, peel]
test_deps = [libboostut, deps]

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

add_project_arguments('-D_GNU_SOURCE',
                      language: 'c')
add_project_arguments('-D_GNU_SOURCE',
                      language: 'cpp')

if get_option('debug')
  add_project_arguments('-DG_ENABLE_DEBUG',
                        language: 'c')
  add_project_arguments('-DG_ENABLE_DEBUG',
                        language: 'cpp')
endif

subdir('res')
subdir('src')
