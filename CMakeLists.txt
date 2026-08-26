cmake_minimum_required(VERSION 3.21)
project(fcitx5-oxpinyin VERSION 0.1.0 LANGUAGES CXX)

set(REQUIRED_FCITX_VERSION 5.1.13)

find_package(PkgConfig REQUIRED)
find_package(Gettext REQUIRED)
find_package(Fcitx5Core ${REQUIRED_FCITX_VERSION} REQUIRED)
find_package(Fcitx5Utils REQUIRED)
# The Punctuation component (fcitx5-chinese-addons) is a HARD dependency, not
# an option: punctuation is delegated to the shared punctuation module via a
# typed call (punctuation_public.h), the way fcitx5's own pinyin does it.
find_package(Fcitx5Module REQUIRED COMPONENTS Spell TestFrontend Punctuation)

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

# Export compile_commands.json for clang-tidy/clangd.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Diagnostic layer on top of Fcitx5CompilerSettings' -Wall -Wextra. The
# selection is probed clean over src/ and test/ on gcc and clang (the source
# carries the corresponding explicit casts/renames; -Wconversion catches the
# size_t->guint narrowing at the engine C ABI), so new warnings that appear
# are new findings, not baseline noise. Keep it that way: a flag that cannot
# be kept warning-free gets scoped or dropped with a reason, not silenced by
# reflex.
option(OXPINYIN_STRICT_WARNINGS
       "Extra diagnostics beyond fcitx5's -Wall/-Wextra defaults" ON)
option(OXPINYIN_WARNINGS_AS_ERRORS
       "Turn warnings into errors (used by the CI diagnostic gate)" OFF)
if(OXPINYIN_STRICT_WARNINGS)
    add_compile_options(-Wpedantic -Wshadow -Wformat=2 -Wundef
                        -Wconversion -Wsign-conversion)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        # High-signal gcc-only checks (probed clean for this tree); null
        # dereference is enabled per-target because it false-positives on
        # FCITX_ASSERT-guarded test code.
        add_compile_options(-Wduplicated-cond -Wduplicated-branches
                            -Wlogical-op)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        add_compile_options(-Wcomma -Wrange-loop-analysis)
    endif()
endif()
if(OXPINYIN_WARNINGS_AS_ERRORS)
    add_compile_options(-Werror)
endif()

set(CMAKE_BUILD_RPATH "${ENGINE_PKG_LIBRARY_DIRS}")

option(ENABLE_TEST "Build test" ON)

# libFuzzer harnesses over the shell's pure input-handling seam. Requires
# clang (-fsanitize=fuzzer); CI runs a short deterministic smoke on PRs and
# a bounded campaign on the nightly schedule.
option(ENABLE_FUZZ "Build the libFuzzer harnesses" OFF)

# Optional Cloud Pinyin via fcitx5-chinese-addons' cloudpinyin module.
# ON additionally pulls in that module's public header and compiles the cloud
# request/inject/direct-commit path. OFF (the default) keeps only the cloud
# code out — the addon needs fcitx5-chinese-addons dev files either way,
# because punctuation is delegated to its punctuation module (hard dep above).
option(ENABLE_CLOUDPINYIN
       "Optional Cloud Pinyin via chinese-addons' cloudpinyin module" OFF)
if (ENABLE_CLOUDPINYIN)
    find_package(Fcitx5Module REQUIRED COMPONENTS CloudPinyin)
endif ()

# Optional lua-driven candidates via fcitx5-lua's LuaAddonLoader module (the
# imeapi addon), the same delegation fcitx5-chinese-addons' pinyin uses. ON
# additionally pulls in that module's public header (luaaddon_public.h) and
# compiles the lua trigger/inject/direct-commit path; the lua interpreter
# itself stays owned by LuaAddonLoader. OFF (the default) keeps all lua code
# out and takes no fcitx5-lua dependency.
option(ENABLE_LUA "Optional lua candidates via fcitx5-lua's imeapi" OFF)
if (ENABLE_LUA)
    find_package(Fcitx5ModuleLuaAddonLoader REQUIRED)
endif ()

# Sanitizer builds link the ASan/UBSan runtime into the test executable, so
# the addon module must be allowed to reference it: strip the
# -Wl,--no-undefined that Fcitx5CompilerSettings adds.
#
# UBSan findings are non-fatal by default (a "runtime error" line and
# continue), which a ctest gate cannot see. -fno-sanitize-recover=undefined
# makes every UB abort, so the executing test fails; ASan is fatal already.
option(ENABLE_SANITIZER "Build with ASan+UBSan" OFF)
if (ENABLE_SANITIZER)
    string(REPLACE "-Wl,--no-undefined" ""
           CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS}")
    string(REPLACE "-Wl,--no-undefined" ""
           CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS}")
    add_compile_options(-fsanitize=address,undefined
                        -fno-sanitize-recover=undefined
                        -fno-omit-frame-pointer)
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
    "Pinyin input method for fcitx5, powered by the ${ENGINE} engine")
set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-or-later")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_CONTACT}")
# fcitx5-chinese-addons is a hard runtime dependency: the addon does not load
# without its punctuation module ([Addon/Dependencies]).
set(CPACK_DEBIAN_PACKAGE_DEPENDS "fcitx5, fcitx5-chinese-addons, ${ENGINE}")
set(CPACK_RPM_PACKAGE_REQUIRES "fcitx5, fcitx5-chinese-addons, ${ENGINE}")
include(CPack)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
