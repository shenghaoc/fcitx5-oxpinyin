cmake_minimum_required(VERSION 3.21)
project(fcitx5-oxpinyin VERSION 0.1.0 LANGUAGES CXX)

set(REQUIRED_FCITX_VERSION 5.1.13)

find_package(PkgConfig REQUIRED)
find_package(Gettext REQUIRED)
find_package(Fcitx5Core ${REQUIRED_FCITX_VERSION} REQUIRED)
find_package(Fcitx5Utils REQUIRED)
find_package(Fcitx5Module REQUIRED COMPONENTS TestFrontend)

set(ENGINE "libpinyin" CACHE STRING "Engine backend (libpinyin or oxpinyin)")
set_property(CACHE ENGINE PROPERTY STRINGS libpinyin oxpinyin)
if(NOT ENGINE MATCHES "^(libpinyin|oxpinyin)$")
    message(FATAL_ERROR "ENGINE must be libpinyin or oxpinyin, got '${ENGINE}'")
endif()

if(ENGINE STREQUAL "oxpinyin")
    pkg_check_modules(ENGINE_PKG REQUIRED IMPORTED_TARGET oxpinyin)
else()
    pkg_check_modules(ENGINE_PKG REQUIRED IMPORTED_TARGET libpinyin)
endif()

include(GNUInstallDirs)
include(FeatureSummary)

if(ENGINE STREQUAL "oxpinyin")
    set(ENGINE_SYSTEM_DATA_DIR "${CMAKE_INSTALL_FULL_DATADIR}/oxpinyin")
else()
    pkg_get_variable(_LIBPINYIN_PKGDATADIR libpinyin pkgdatadir)
    set(ENGINE_SYSTEM_DATA_DIR "${_LIBPINYIN_PKGDATADIR}/data")
endif()

include("${FCITX_INSTALL_CMAKECONFIG_DIR}/Fcitx5Utils/Fcitx5CompilerSettings.cmake")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_BUILD_RPATH "${ENGINE_PKG_LIBRARY_DIRS}")

option(ENABLE_TEST "Build test" ON)

# English input mode (ported from ibus-libpinyin's EnglishEditor): typing
# v opens a prefix-matched English word list backed by SQLite.  Optional
# so the sqlite3 dependency stays opt-in, mirroring upstream's
# --enable-english-input-mode (default enabled there too).
option(ENABLE_ENGLISH_INPUT_MODE
       "Build the English input mode (requires SQLite)" ON)
if (ENABLE_ENGLISH_INPUT_MODE)
    pkg_check_modules(SQLITE3 REQUIRED IMPORTED_TARGET sqlite3)
    find_program(AWK_EXECUTABLE NAMES gawk awk mawk REQUIRED)
    find_program(SQLITE3_EXECUTABLE NAMES sqlite3 REQUIRED)
endif ()

# Sanitizer builds link the ASan/UBSan runtime into the test executable, so
# the addon module must be allowed to reference it: strip the
# -Wl,--no-undefined that Fcitx5CompilerSettings adds.
option(ENABLE_SANITIZER "Build with ASan+UBSan" OFF)
if (ENABLE_SANITIZER)
    string(REPLACE "-Wl,--no-undefined" ""
           CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS}")
    string(REPLACE "-Wl,--no-undefined" ""
           CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS}")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif ()

add_subdirectory(src)
add_subdirectory(po)
if (ENABLE_ENGLISH_INPUT_MODE)
    add_subdirectory(data)
endif ()

fcitx5_translate_desktop_file(org.fcitx.Fcitx5.Addon.Oxpinyin.metainfo.xml.in
                              org.fcitx.Fcitx5.Addon.Oxpinyin.metainfo.xml XML)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/org.fcitx.Fcitx5.Addon.Oxpinyin.metainfo.xml"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/metainfo")

if (ENABLE_TEST)
    enable_testing()
    add_subdirectory(test)
endif ()

# CPack: generated off the install() rules with `cpack -G "DEB;RPM"`.
# Archive-grade debian/ + .spec packaging is a later, separate effort.
set(CPACK_PACKAGE_NAME "fcitx5-oxpinyin")
set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
set(CPACK_PACKAGE_CONTACT "Shenghao Chen <shenghaoc@outlook.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Pinyin input method for fcitx5, powered by the ${ENGINE} engine")
set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-or-later")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_CONTACT}")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "fcitx5, ${ENGINE}")
set(CPACK_RPM_PACKAGE_REQUIRES "fcitx5, ${ENGINE}")
include(CPack)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
