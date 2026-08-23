set(OXPINYIN_SOURCES oxpinyin.cpp punctuation.cpp)
if (ENABLE_ENGLISH_INPUT_MODE)
    list(APPEND OXPINYIN_SOURCES englisheditor.cpp englishdatabase.cpp)
endif ()
add_fcitx5_addon(oxpinyin ${OXPINYIN_SOURCES})
set_target_properties(oxpinyin PROPERTIES PREFIX "")
set(OXPINYIN_ENGLISH_DB_INSTALL_PATH
    "${CMAKE_INSTALL_FULL_DATADIR}/fcitx5/oxpinyin/db/english.db")
configure_file(oxpinyin_paths.h.in oxpinyin_paths.h)
target_link_libraries(oxpinyin Fcitx5::Core Fcitx5::Config PkgConfig::ENGINE_PKG)
if (ENABLE_ENGLISH_INPUT_MODE)
    target_compile_definitions(oxpinyin PRIVATE OXPINYIN_ENGLISH_INPUT_MODE)
    target_link_libraries(oxpinyin PkgConfig::SQLITE3)
endif ()
target_include_directories(oxpinyin PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_compile_definitions(oxpinyin PRIVATE FCITX_GETTEXT_DOMAIN=\"fcitx5-oxpinyin\")
fcitx5_add_i18n_definition(TARGETS oxpinyin)
install(TARGETS oxpinyin DESTINATION "${CMAKE_INSTALL_LIBDIR}/fcitx5")

fcitx5_translate_desktop_file(oxpinyin.conf.in oxpinyin.conf)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/oxpinyin.conf"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/inputmethod" COMPONENT config)

configure_file(oxpinyin-addon.conf.in.in oxpinyin-addon.conf.in)
fcitx5_translate_desktop_file("${CMAKE_CURRENT_BINARY_DIR}/oxpinyin-addon.conf.in"
                              oxpinyin-addon.conf)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/oxpinyin-addon.conf"
        RENAME oxpinyin.conf DESTINATION "${FCITX_INSTALL_PKGDATADIR}/addon"
        COMPONENT config)
