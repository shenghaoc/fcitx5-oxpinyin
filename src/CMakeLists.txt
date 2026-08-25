add_fcitx5_addon(oxpinyin oxpinyin.cpp)
set_target_properties(oxpinyin PROPERTIES PREFIX "")
configure_file(oxpinyin_paths.h.in oxpinyin_paths.h)
# Fcitx5::Module::Punctuation is UNCONDITIONAL: punctuation is delegated to
# fcitx5-chinese-addons' shared punctuation module (a hard dependency; the
# interface target carries the include dir for punctuation_public.h, and the
# module itself is reached at runtime over the addon ABI).
target_link_libraries(oxpinyin Fcitx5::Core Fcitx5::Config
                      Fcitx5::Module::Spell Fcitx5::Module::Punctuation
                      PkgConfig::ENGINE_PKG)
target_include_directories(oxpinyin PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

# ENABLE_CLOUDPINYIN pulls in the cloudpinyin module's public header (the
# Fcitx5::Module::CloudPinyin interface target carries only its include dir;
# the module itself is reached at runtime over the addon ABI, never linked)
# and flips on the guarded cloud code. OFF: no cloud code or cloud header
# (the punctuation module's header is always present — hard dependency).
if (ENABLE_CLOUDPINYIN)
    target_link_libraries(oxpinyin Fcitx5::Module::CloudPinyin)
    target_compile_definitions(oxpinyin PRIVATE OXPINYIN_ENABLE_CLOUDPINYIN)
endif ()
target_compile_definitions(oxpinyin PRIVATE FCITX_GETTEXT_DOMAIN=\"fcitx5-oxpinyin\")
fcitx5_add_i18n_definition(TARGETS oxpinyin)
install(TARGETS oxpinyin DESTINATION "${CMAKE_INSTALL_LIBDIR}/fcitx5")

fcitx5_translate_desktop_file(oxpinyin.conf.in oxpinyin.conf)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/oxpinyin.conf"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/inputmethod" COMPONENT config)

# cloudpinyin is advertised as an optional dependency only when the cloud code
# is actually built (ENABLE_CLOUDPINYIN). OFF leaves the manifest byte-for-byte
# the header-free baseline — the leading newline keeps the entry on its own line
# after 5=pinyinhelper.
if (ENABLE_CLOUDPINYIN)
    set(OXPINYIN_CLOUDPINYIN_OPTDEP "\n6=cloudpinyin")
else ()
    set(OXPINYIN_CLOUDPINYIN_OPTDEP "")
endif ()
configure_file(oxpinyin-addon.conf.in.in oxpinyin-addon.conf.in)
fcitx5_translate_desktop_file("${CMAKE_CURRENT_BINARY_DIR}/oxpinyin-addon.conf.in"
                              oxpinyin-addon.conf)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/oxpinyin-addon.conf"
        RENAME oxpinyin.conf DESTINATION "${FCITX_INSTALL_PKGDATADIR}/addon"
        COMPONENT config)
