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
    # Hermetic synchronous cloudpinyin stub addon (test-only), built before the
    # runners that load it.
    add_subdirectory(cloudpinyin-stub)
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
    # A distinct process exercises the module-absent guard: the addon is built
    # with OXPINYIN_ENABLE_CLOUDPINYIN (ENABLE_CLOUDPINYIN ON) but this harness
    # loads NO cloudpinyin module -- it is absent from --enable and no
    # cloudpinyin datadir is wired in (the module must stay unreachable). With
    # CloudPinyinEnabled flipped on, cloudpinyin() is null, no cloud row is
    # injected, and normal composition + commit are intact. Spell stays loaded
    # so this reuses the full normal-composition suite, isolating cloud-absent
    # from the no-spell runner. Keep the datadir off so cloudpinyin.conf is not
    # discoverable here; --enable already keeps it from loading.
    add_executable(testoxpinyin-cloudabsent testoxpinyin.cpp)
    target_compile_definitions(testoxpinyin-cloudabsent PRIVATE
                               OXPINYIN_ENABLE_CLOUDPINYIN
                               OXPINYIN_TEST_CLOUD_ABSENT)
    target_link_libraries(testoxpinyin-cloudabsent Fcitx5::Core
                          Fcitx5::Module::TestFrontend)
    add_dependencies(testoxpinyin-cloudabsent copy-addon copy-im)
    add_test(testoxpinyin-cloudabsent testoxpinyin-cloudabsent)

    # Hermetic cloud-select runner: loads the in-tree STUB cloudpinyin addon
    # (NOT the real module), which fills synchronously, so the full
    # row->select->commit path is testable with no network. The addon/data dirs
    # point ONLY at the stub dir (plus build/src for oxpinyin.so and the system
    # pkgdatadir for Spell data); the stub's cloudpinyin.conf (under the
    # build-root path) wins addon discovery over any system real cloudpinyin
    # conf, so cloudpinyin() resolves to the stub. Defines the canned-hanzi
    # macro shared with the stub addon for the assertion.
    add_executable(testoxpinyin-cloudstub testoxpinyin.cpp)
    target_compile_definitions(testoxpinyin-cloudstub PRIVATE
                               OXPINYIN_ENABLE_CLOUDPINYIN
                               OXPINYIN_TEST_CLOUD_STUB)
    target_link_libraries(testoxpinyin-cloudstub Fcitx5::Core
                          Fcitx5::Module::TestFrontend)
    target_include_directories(testoxpinyin-cloudstub PRIVATE
                               ${CMAKE_CURRENT_SOURCE_DIR}/cloudpinyin-stub)
    add_dependencies(testoxpinyin-cloudstub copy-addon copy-im cloudpinyinstub)
    add_test(testoxpinyin-cloudstub testoxpinyin-cloudstub)
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
    set(_oxpinyin_tests testoxpinyin testoxpinyin-nospell)
    if (ENABLE_CLOUDPINYIN)
        list(APPEND _oxpinyin_tests testoxpinyin-cloudabsent
             testoxpinyin-cloudstub)
    endif ()
    set_tests_properties(${_oxpinyin_tests} PROPERTIES ENVIRONMENT
                                                   "${OXPINYIN_TEST_ENV}")
endif ()
