add_custom_target(copy-addon DEPENDS oxpinyin-addon.conf.in-fmt)
add_custom_command(TARGET copy-addon
                   COMMAND ${CMAKE_COMMAND} -E copy
                           ${PROJECT_BINARY_DIR}/src/oxpinyin-addon.conf
                           ${CMAKE_CURRENT_BINARY_DIR}/oxpinyin.conf)
