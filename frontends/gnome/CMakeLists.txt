# GNOME Shell extension. Nothing is compiled except the GSettings schema and translations.
#
# Targets:
#   (all)                 compiles the schema into the build tree
#   gnome-extension-zip   builds <uuid>.shell-extension.zip with `gnome-extensions pack`
#   gnome-extension-pot   regenerates po/readout.pot with xgettext

set(READOUT_GNOME_UUID "readout@aakash1122.github.io")
set(READOUT_GNOME_SCHEMA "org.gnome.shell.extensions.readout")
set(READOUT_GNOME_INSTALL_DIR "${CMAKE_INSTALL_DATADIR}/gnome-shell/extensions/${READOUT_GNOME_UUID}")

find_program(GLIB_COMPILE_SCHEMAS glib-compile-schemas REQUIRED)
find_program(GNOME_EXTENSIONS_TOOL gnome-extensions)
find_program(XGETTEXT_TOOL xgettext)
find_program(MSGFMT_TOOL msgfmt)

set(extension_sources
    extension.js
    prefs.js
    metadata.json
    stylesheet.css
    stylesheet-dark.css
    stylesheet-light.css
)
set(extension_library_sources
    lib/alerts.js
    lib/catalog.js
    lib/charts.js
    lib/components.js
    lib/dbusClient.js
    lib/format.js
    lib/panelItem.js
    lib/panelWidgets.js
    lib/popups.js
    lib/summaries.js
    lib/theme.js
    lib/values.js
)
file(GLOB extension_icons CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/icons/*.svg")
set(schema_source "${CMAKE_CURRENT_SOURCE_DIR}/schemas/${READOUT_GNOME_SCHEMA}.gschema.xml")

# The compiled schema ships inside the extension directory, where getSettings() finds it.
set(schema_build_dir "${CMAKE_CURRENT_BINARY_DIR}/schemas")
add_custom_command(
    OUTPUT "${schema_build_dir}/gschemas.compiled"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${schema_build_dir}"
    COMMAND ${GLIB_COMPILE_SCHEMAS} --strict --targetdir "${schema_build_dir}" "${CMAKE_CURRENT_SOURCE_DIR}/schemas"
    DEPENDS "${schema_source}"
    COMMENT "Compiling the Readout GNOME Shell extension schema"
    VERBATIM
)
set(extension_build_outputs "${schema_build_dir}/gschemas.compiled")

# Translations: po/<lang>.po listed in po/LINGUAS become locale/<lang>/LC_MESSAGES/readout.mo.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/po/LINGUAS" extension_languages REGEX "^[^#]")
set(extension_locale_dir "${CMAKE_CURRENT_BINARY_DIR}/locale")
if(extension_languages)
    if(NOT MSGFMT_TOOL)
        message(FATAL_ERROR "msgfmt is required to build the GNOME extension translations")
    endif()
    foreach(language IN LISTS extension_languages)
        string(STRIP "${language}" language)
        set(mo_file "${extension_locale_dir}/${language}/LC_MESSAGES/readout.mo")
        add_custom_command(
            OUTPUT "${mo_file}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${extension_locale_dir}/${language}/LC_MESSAGES"
            COMMAND ${MSGFMT_TOOL} --check -o "${mo_file}" "${CMAKE_CURRENT_SOURCE_DIR}/po/${language}.po"
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/po/${language}.po"
            VERBATIM
        )
        list(APPEND extension_build_outputs "${mo_file}")
    endforeach()
endif()

add_custom_target(gnome-extension ALL DEPENDS ${extension_build_outputs})

install(FILES ${extension_sources} DESTINATION "${READOUT_GNOME_INSTALL_DIR}")
install(FILES ${extension_library_sources} DESTINATION "${READOUT_GNOME_INSTALL_DIR}/lib")
install(FILES ${extension_icons} DESTINATION "${READOUT_GNOME_INSTALL_DIR}/icons")
install(FILES "${schema_source}" "${schema_build_dir}/gschemas.compiled"
    DESTINATION "${READOUT_GNOME_INSTALL_DIR}/schemas")
if(extension_languages)
    install(DIRECTORY "${extension_locale_dir}/" DESTINATION "${READOUT_GNOME_INSTALL_DIR}/locale")
endif()

if(GNOME_EXTENSIONS_TOOL)
    set(extension_zip "${CMAKE_CURRENT_BINARY_DIR}/${READOUT_GNOME_UUID}.shell-extension.zip")
    set(pack_extra_sources)
    foreach(extra IN ITEMS lib icons stylesheet-dark.css stylesheet-light.css)
        list(APPEND pack_extra_sources "--extra-source=${CMAKE_CURRENT_SOURCE_DIR}/${extra}")
    endforeach()
    add_custom_command(
        OUTPUT "${extension_zip}"
        COMMAND ${GNOME_EXTENSIONS_TOOL} pack --force
            --out-dir "${CMAKE_CURRENT_BINARY_DIR}"
            --podir "${CMAKE_CURRENT_SOURCE_DIR}/po"
            ${pack_extra_sources}
            "${CMAKE_CURRENT_SOURCE_DIR}"
        DEPENDS ${extension_sources} ${extension_library_sources} ${extension_icons} "${schema_source}"
        COMMENT "Packing ${READOUT_GNOME_UUID}.shell-extension.zip"
        VERBATIM
    )
    add_custom_target(gnome-extension-zip DEPENDS "${extension_zip}")
else()
    message(STATUS "gnome-extensions not found: the gnome-extension-zip target is not available")
endif()

if(XGETTEXT_TOOL)
    add_custom_target(gnome-extension-pot
        COMMAND ${XGETTEXT_TOOL} --from-code=UTF-8 --add-comments --keyword=_ --keyword=N_
            --package-name=readout --package-version=${PROJECT_VERSION}
            --files-from=po/POTFILES --directory=. --output=po/readout.pot
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Extracting translatable strings into po/readout.pot"
        VERBATIM
    )
endif()
