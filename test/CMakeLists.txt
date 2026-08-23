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
