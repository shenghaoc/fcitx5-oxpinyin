# Catalogs shipped are exactly the languages in LINGUAS, and every .po file in
# this directory must be listed there: fcitx5_translate_desktop_file() globs
# *.po for the manifest/metainfo translations, so a catalog missing from
# LINGUAS would translate the manifests but silently not ship its .mo.
set(OXPINYIN_GETTEXT_DOMAIN fcitx5-oxpinyin)
file(STRINGS LINGUAS _linguas_lines)
set(OXPINYIN_LINGUAS "")
foreach(_line IN LISTS _linguas_lines)
    string(STRIP "${_line}" _line)
    if(_line AND NOT _line MATCHES "^#")
        list(APPEND OXPINYIN_LINGUAS "${_line}")
    endif()
endforeach()
file(GLOB _po_files RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" *.po)
set(_po_langs "")
foreach(_po IN LISTS _po_files)
    get_filename_component(_lang "${_po}" NAME_WE)
    list(APPEND _po_langs "${_lang}")
endforeach()
list(SORT OXPINYIN_LINGUAS)
list(SORT _po_langs)
if(NOT OXPINYIN_LINGUAS STREQUAL _po_langs)
    message(FATAL_ERROR
            "po/LINGUAS (${OXPINYIN_LINGUAS}) does not match the catalogs "
            "present (${_po_langs}).")
endif()

# Compile each catalog and install it under the relocatable GNUInstallDirs
# locale directory (fcitx5_install_translation() installs to the absolute
# FCITX_INSTALL_LOCALEDIR, which escapes `cmake --install --prefix=`).
set(_mo_files "")
foreach(_lang IN LISTS OXPINYIN_LINGUAS)
    set(_mo "${CMAKE_CURRENT_BINARY_DIR}/${_lang}.mo")
    add_custom_command(
        OUTPUT "${_mo}"
        COMMAND "${GETTEXT_MSGFMT_EXECUTABLE}" --check --check-format
                -o "${_mo}" "${CMAKE_CURRENT_SOURCE_DIR}/${_lang}.po"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${_lang}.po")
    install(FILES "${_mo}" RENAME "${OXPINYIN_GETTEXT_DOMAIN}.mo"
            DESTINATION "${CMAKE_INSTALL_LOCALEDIR}/${_lang}/LC_MESSAGES"
            COMPONENT translation)
    list(APPEND _mo_files "${_mo}")
endforeach()
add_custom_target(${OXPINYIN_GETTEXT_DOMAIN}-translation ALL
                  DEPENDS ${_mo_files})
