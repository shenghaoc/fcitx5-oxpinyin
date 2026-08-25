# Hermetic cloudpinyin test stub. Built only under ENABLE_CLOUDPINYIN (it
# #includes cloudpinyin_public.h from fcitx5-chinese-addons, available there).
# The stub .so is named cloudpinyinstub (NOT cloudpinyin) so it never clashes
# with a real installed cloudpinyin module; the in-tree cloudpinyin.conf points
# the "cloudpinyin" addon at this Library, and the cloud-stub runner wires only
# THIS dir into its addon/data dirs so cloudpinyin() resolves to the stub.

add_fcitx5_addon(cloudpinyinstub stubcloudpinyin.cpp)
set_target_properties(cloudpinyinstub PROPERTIES PREFIX "")
target_link_libraries(cloudpinyinstub Fcitx5::Core Fcitx5::Module::CloudPinyin)
target_include_directories(cloudpinyinstub PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Lay out cloudpinyinstub.so and addon/cloudpinyin.conf in this build dir; the
# cloud-stub runner adds THIS dir to both its addon dirs (.so lookup) and data
# dirs (addon/*.conf discovery).
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/addon")
configure_file(cloudpinyin.conf.in "${CMAKE_CURRENT_BINARY_DIR}/addon/cloudpinyin.conf" @ONLY)
