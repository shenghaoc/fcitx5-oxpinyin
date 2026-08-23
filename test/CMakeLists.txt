configure_file(testdir.h.in testdir.h @ONLY)
include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../src)

add_subdirectory(addon)
add_subdirectory(inputmethod)

add_executable(testoxpinyin testoxpinyin.cpp)
target_link_libraries(testoxpinyin Fcitx5::Core Fcitx5::Module::TestFrontend)
add_dependencies(testoxpinyin copy-addon copy-im)
if (ENABLE_ENGLISH_INPUT_MODE)
    target_compile_definitions(testoxpinyin PRIVATE OXPINYIN_ENGLISH_INPUT_MODE)
    # The frequency-order parity check re-runs the upstream SQL against
    # the same database the addon queries.
    target_link_libraries(testoxpinyin PkgConfig::SQLITE3)
    add_dependencies(testoxpinyin english-db)
endif ()
add_test(testoxpinyin testoxpinyin)

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
if (ENABLE_ENGLISH_INPUT_MODE)
    list(APPEND OXPINYIN_TEST_ENV
         "OXPINYIN_ENGLISH_SYSTEM_DB=${CMAKE_BINARY_DIR}/data/english.db")
endif ()
if (OXPINYIN_TEST_ENV)
    set_tests_properties(testoxpinyin PROPERTIES ENVIRONMENT
                                                   "${OXPINYIN_TEST_ENV}")
endif ()
