configure_file(testdir.h.in testdir.h @ONLY)
include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../src)

add_subdirectory(addon)
add_subdirectory(inputmethod)

add_executable(testoxpinyin testoxpinyin.cpp)
target_link_libraries(testoxpinyin Fcitx5::Core Fcitx5::Module::TestFrontend)
add_dependencies(testoxpinyin copy-addon copy-im)
add_test(testoxpinyin testoxpinyin)

# Pass configure-time engine data dirs through to the test when given
# (-DOXPINYIN_SYSTEM_DATA_DIR=... / -DOXPINYIN_USER_DATA_DIR=...); the
# environment variables always win at runtime.
set(OXPINYIN_TEST_ENV "")
if (OXPINYIN_SYSTEM_DATA_DIR)
    list(APPEND OXPINYIN_TEST_ENV
         "OXPINYIN_SYSTEM_DATA_DIR=${OXPINYIN_SYSTEM_DATA_DIR}")
endif ()
if (OXPINYIN_USER_DATA_DIR)
    list(APPEND OXPINYIN_TEST_ENV
         "OXPINYIN_USER_DATA_DIR=${OXPINYIN_USER_DATA_DIR}")
endif ()
if (OXPINYIN_TEST_ENV)
    set_tests_properties(testoxpinyin PROPERTIES ENVIRONMENT
                                                   "${OXPINYIN_TEST_ENV}")
endif ()
