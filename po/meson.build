i18n = import('i18n')
add_project_arguments('-DG_LOG_DOMAIN="MNN"', '-DGETTEXT_PACKAGE="MNN"', language: ['c', 'cpp'])
i18n.gettext('MNN',
             args: ['--directory=' + meson.project_source_root(),
                    '--no-location'],
             preset: 'glib')
