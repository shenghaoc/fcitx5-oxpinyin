configure_file(testdir.h.in testdir.h @ONLY)
include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../src)

add_subdirectory(addon)
add_subdirectory(inputmethod)

add_executable(testoxpinyin testoxpinyin.cpp)
target_link_libraries(testoxpinyin Fcitx5::Core Fcitx5::Module::TestFrontend)
add_dependencies(testoxpinyin copy-addon copy-im)
add_test(testoxpinyin testoxpinyin)

# A distinct process keeps the engine's lazy Spell-addon pointer null for the
# entire run, verifying that a disabled module never blocks normal pinyin.
add_executable(testoxpinyin-nospell testoxpinyin.cpp)
target_compile_definitions(testoxpinyin-nospell PRIVATE OXPINYIN_TEST_NO_SPELL)
target_link_libraries(testoxpinyin-nospell Fcitx5::Core
                      Fcitx5::Module::TestFrontend)
add_dependencies(testoxpinyin-nospell copy-addon copy-im)
add_test(testoxpinyin-nospell testoxpinyin-nospell)

# ENABLE_CLOUDPINYIN: the cloud tests drive the REAL fcitx5-chinese-addons
# cloudpinyin module. main() already adds fcitxPath("pkgdatadir") to the harness
# data dirs (for the Spell module), which suffices when cloudpinyin is installed
# under the same prefix as fcitx5 core (the usual distro case). But cloudpinyin
# may be discovered via CMAKE_PREFIX_PATH at a DIFFERENT prefix, and then its
# cloudpinyin.conf is NOT under fcitx5 core's pkgdatadir. Derive that prefix from
# the discovered module target's include dir
#   <prefix>/include/Fcitx5/Module/fcitx-module/cloudpinyin
# and hand its share/fcitx5 to the harness so cloudpinyin.conf is always
# reachable, whatever prefix the module came from.
if (ENABLE_CLOUDPINYIN)
    get_target_property(_cloudpinyin_incdir Fcitx5::Module::CloudPinyin
                        INTERFACE_INCLUDE_DIRECTORIES)
    set(_cloudpinyin_prefix "${_cloudpinyin_incdir}")
    foreach (_ RANGE 4) # strip include/Fcitx5/Module/fcitx-module/cloudpinyin
        get_filename_component(_cloudpinyin_prefix "${_cloudpinyin_prefix}"
                               DIRECTORY)
    endforeach ()
    target_compile_definitions(testoxpinyin PRIVATE
                               OXPINYIN_ENABLE_CLOUDPINYIN
                               OXPINYIN_TEST_CLOUDPINYIN
                               "OXPINYIN_CLOUDPINYIN_DATADIR=\"${_cloudpinyin_prefix}/share/fcitx5\"")
endif ()

# Pass the engine data directory to the test as an environment variable;
# the explicit -DOXPINYIN_SYSTEM_DATA_DIR=... override wins, otherwise
# the ENGINE_SYSTEM_DATA_DIR resolved by the engine selector is used.
if (NOT OXPINYIN_SYSTEM_DATA_DIR)
    set(OXPINYIN_SYSTEM_DATA_DIR "${ENGINE_SYSTEM_DATA_DIR}")
endif ()
set(OXPINYIN_TEST_ENV "OXPINYIN_SYSTEM_DATA_DIR=${OXPINYIN_SYSTEM_DATA_DIR}")
if (OXPINYIN_USER_DATA_DIR)
    list(APPEND OXPINYIN_TEST_ENV
         "OXPINYIN_USER_DATA_DIR=${OXPINYIN_USER_DATA_DIR}")
endif ()
if (OXPINYIN_TEST_ENV)
    set_tests_properties(testoxpinyin testoxpinyin-nospell PROPERTIES ENVIRONMENT
                                                   "${OXPINYIN_TEST_ENV}")
endif ()
