cmake_minimum_required(VERSION 3.21)
project(fcitx5-oxpinyin VERSION 0.1.0 LANGUAGES CXX)

set(REQUIRED_FCITX_VERSION 5.1.13)

find_package(PkgConfig REQUIRED)
find_package(Gettext REQUIRED)
find_package(Fcitx5Core ${REQUIRED_FCITX_VERSION} REQUIRED)
find_package(Fcitx5Utils REQUIRED)
find_package(Fcitx5Module REQUIRED COMPONENTS TestFrontend)

pkg_check_modules(OXPINYIN REQUIRED IMPORTED_TARGET oxpinyin)

include(GNUInstallDirs)
include(FeatureSummary)

include("${FCITX_INSTALL_CMAKECONFIG_DIR}/Fcitx5Utils/Fcitx5CompilerSettings.cmake")
# fcitx5's own floor is C++17; this shell is C++20 (complete on every
# target, incl. Ubuntu 24.04 GCC 13). C++23 is not required.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Let build-tree test binaries find libpinyin_capi.so when the engine is
# installed to a pkg-config prefix outside the default loader path.
set(CMAKE_BUILD_RPATH "${OXPINYIN_LIBRARY_DIRS}")

option(ENABLE_TEST "Build test" ON)

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
    "Pinyin input method for fcitx5, powered by the oxpinyin engine")
set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-or-later")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_CONTACT}")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "fcitx5, oxpinyin")
set(CPACK_RPM_PACKAGE_REQUIRES "fcitx5, oxpinyin")
include(CPack)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
