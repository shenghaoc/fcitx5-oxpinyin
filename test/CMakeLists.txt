configure_file(testdir.h.in testdir.h @ONLY)
include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../src)

add_subdirectory(addon)
add_subdirectory(inputmethod)

# Fcitx5::Module::Punctuation carries the include dir for
# punctuation_public.h: the tests read the expected delegated punctuation
# from the module itself (typed getPunctuation call), never hardcoding the
# mapping data. The module runs in-process via the addon manager (--enable),
# it is not linked.
add_executable(testoxpinyin testoxpinyin.cpp)
target_link_libraries(testoxpinyin Fcitx5::Core Fcitx5::Module::TestFrontend
                      Fcitx5::Module::Punctuation)
add_dependencies(testoxpinyin copy-addon copy-im)
add_test(testoxpinyin testoxpinyin)

# A distinct process keeps the engine's lazy Spell-addon pointer null for the
# entire run, verifying that a disabled module never blocks normal pinyin.
# The punctuation module stays enabled here (as in every engine-loading
# runner): it is a hard dependency.
add_executable(testoxpinyin-nospell testoxpinyin.cpp)
target_compile_definitions(testoxpinyin-nospell PRIVATE OXPINYIN_TEST_NO_SPELL)
target_link_libraries(testoxpinyin-nospell Fcitx5::Core
                      Fcitx5::Module::TestFrontend
                      Fcitx5::Module::Punctuation)
add_dependencies(testoxpinyin-nospell copy-addon copy-im)
add_test(testoxpinyin-nospell testoxpinyin-nospell)

# Punctuation is a HARD dependency ([Addon/Dependencies]): fcitx5's addon
# manager refuses to load the engine when the punctuation module is not
# loadable. This dedicated runner deselects the module and pins exactly that
# load-time enforcement — the inverted successor of the old "addon loads
# without chinese-addons" invariant.
add_executable(testoxpinyin-punctabsent testoxpinyin-punctabsent.cpp)
target_link_libraries(testoxpinyin-punctabsent Fcitx5::Core
                      Fcitx5::Module::TestFrontend)
add_dependencies(testoxpinyin-punctabsent copy-addon copy-im)
add_test(testoxpinyin-punctabsent testoxpinyin-punctabsent)

# Conversion-modules runner: chinese-addons' chttrans + fullwidth modules are
# ENABLED so their toggle Actions land in the status area next to the
# punctuation module's and this addon's own (the PR #8 wiring is what adds
# them; this addon never re-creates them). Both conversions are dormant by
# default (fullwidth off; chttrans enables per-IM only after its toggle), so
# the normal-composition suite runs unchanged here too.
add_executable(testoxpinyin-conv testoxpinyin.cpp)
target_compile_definitions(testoxpinyin-conv PRIVATE OXPINYIN_TEST_CONV)
target_link_libraries(testoxpinyin-conv Fcitx5::Core
                      Fcitx5::Module::TestFrontend
                      Fcitx5::Module::Punctuation)
add_dependencies(testoxpinyin-conv copy-addon copy-im)
add_test(testoxpinyin-conv testoxpinyin-conv)

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
                          Fcitx5::Module::TestFrontend
                          Fcitx5::Module::Punctuation)
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
                          Fcitx5::Module::TestFrontend
                          Fcitx5::Module::Punctuation)
    target_include_directories(testoxpinyin-cloudstub PRIVATE
                               ${CMAKE_CURRENT_SOURCE_DIR}/cloudpinyin-stub)
    add_dependencies(testoxpinyin-cloudstub copy-addon copy-im cloudpinyinstub)
    add_test(testoxpinyin-cloudstub testoxpinyin-cloudstub)
endif ()

# ENABLE_LUA: the lua tests drive the REAL fcitx5-lua modules (luaaddonloader
# + imeapi) plus a canned in-tree imeapi extension. main() already adds
# fcitxPath("pkgdatadir") to the harness data dirs, which suffices when
# fcitx5-lua is installed under the same prefix as fcitx5 core (the usual
# distro case). But it may be discovered via CMAKE_PREFIX_PATH at a DIFFERENT
# prefix, and then its addon confs and lua/imeapi/imeapi.lua are NOT under
# fcitx5 core's pkgdatadir. Derive that prefix from the discovered module
# target's include dir
#   <prefix>/include/Fcitx5/Module/fcitx-module/luaaddonloader
# and hand its share/fcitx5 to the harness so imeapi is always loadable,
# whatever prefix the module came from.
if (ENABLE_LUA)
    get_target_property(_lua_incdir Fcitx5::Module::LuaAddonLoader
                        INTERFACE_INCLUDE_DIRECTORIES)
    set(_lua_prefix "${_lua_incdir}")
    foreach (_ RANGE 4) # strip include/Fcitx5/Module/fcitx-module/luaaddonloader
        get_filename_component(_lua_prefix "${_lua_prefix}" DIRECTORY)
    endforeach ()
    target_compile_definitions(testoxpinyin PRIVATE
                               OXPINYIN_ENABLE_LUA
                               OXPINYIN_TEST_LUA
                               "OXPINYIN_LUA_DATADIR=\"${_lua_prefix}/share/fcitx5\"")
    # Canned imeapi extension: imeapi loads *.lua from
    # <PkgData>/lua/imeapi/extensions, and the harness data dirs include this
    # build/test dir — so copying it here is enough for the loader to find it.
    file(COPY lua/imeapi/extensions/oxpinyintest.lua
         DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/lua/imeapi/extensions")

    # A distinct process exercises the imeapi-absent guard: the addon is built
    # with OXPINYIN_ENABLE_LUA but this harness enables NO lua modules —
    # luaaddonloader/imeapi stay off the --enable list, imeapi() is null, no
    # lua rows are injected, and normal composition + commit are intact. Spell
    # stays loaded so this reuses the full normal-composition suite.
    add_executable(testoxpinyin-luaabsent testoxpinyin.cpp)
    target_compile_definitions(testoxpinyin-luaabsent PRIVATE
                               OXPINYIN_ENABLE_LUA
                               OXPINYIN_TEST_LUA_ABSENT)
    target_link_libraries(testoxpinyin-luaabsent Fcitx5::Core
                          Fcitx5::Module::TestFrontend
                          Fcitx5::Module::Punctuation)
    add_dependencies(testoxpinyin-luaabsent copy-addon copy-im)
    add_test(testoxpinyin-luaabsent testoxpinyin-luaabsent)
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
# LeakSanitizer: suppress the known upstream libpinyin leak (see the
# suppression file for the bug and scoping). Only ASan-instrumented builds
# read LSAN_OPTIONS; it is ignored elsewhere.
list(APPEND OXPINYIN_TEST_ENV
     "LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/lsan-suppressions.txt")
if (OXPINYIN_TEST_ENV)
    set(_oxpinyin_tests testoxpinyin testoxpinyin-nospell
        testoxpinyin-punctabsent testoxpinyin-conv)
    if (ENABLE_CLOUDPINYIN)
        list(APPEND _oxpinyin_tests testoxpinyin-cloudabsent
             testoxpinyin-cloudstub)
    endif ()
    if (ENABLE_LUA)
        list(APPEND _oxpinyin_tests testoxpinyin-luaabsent)
    endif ()
    set_tests_properties(${_oxpinyin_tests} PROPERTIES ENVIRONMENT
                                                   "${OXPINYIN_TEST_ENV}")
endif ()
