# SPDX-License-Identifier: GPL-3.0-or-later
#
# License files, and the Debian changelog, for the CPack packages. CPack runs
# this script (through CPACK_INSTALL_SCRIPTS) while it stages a package, and
# nothing else does: `cmake --install` never reads it, so a plain install and
# the Fedora-native RPM built from one (which ships LICENSE through the spec's
# %license) gain no file. CPACK_GENERATOR is the generator being staged.
#
#   DEB    <prefix>/share/doc/<package>/copyright   (Debian policy 12.5)
#          <prefix>/share/doc/<package>/changelog.gz  (Debian policy 12.7)
#   RPM    <prefix>/share/licenses/<package>/LICENSE
#   other  <prefix>/share/doc/<package>/LICENSE     (TXZ and other archives)

# CPack stages either below $DESTDIR (DEB, RPM) or with CMAKE_INSTALL_PREFIX
# already inside its staging tree (archives). Anything else would write to the
# live prefix, so refuse it.
string(FIND "${CMAKE_INSTALL_PREFIX}" "${CPACK_TEMPORARY_DIRECTORY}" _staged)
if(NOT CPACK_GENERATOR OR ("$ENV{DESTDIR}" STREQUAL "" AND NOT _staged EQUAL 0))
    message(FATAL_ERROR
            "license-files.cmake only runs inside a CPack staging install")
endif()

set(_source_dir "${CMAKE_CURRENT_LIST_DIR}/../..")
if(CPACK_GENERATOR STREQUAL "DEB")
    # Debian keeps licence terms in a copyright file that points at
    # /usr/share/common-licenses, not in a copy of the licence text.
    file(INSTALL "${_source_dir}/packaging/debian/copyright"
         DESTINATION "${CMAKE_INSTALL_PREFIX}/share/doc/${CPACK_PACKAGE_NAME}")
    # The changelog ships compressed with `gzip -9n` (maximum compression, no
    # timestamp). It is compressed beside the staging tree, not inside it, so
    # that file(INSTALL) applies $DESTDIR and the usual 0644 mode.
    execute_process(
        COMMAND gzip -9n -c "${_source_dir}/packaging/debian/changelog"
        OUTPUT_FILE "${CPACK_TOPLEVEL_DIRECTORY}/changelog.gz"
        RESULT_VARIABLE _gzip_result)
    if(NOT _gzip_result EQUAL 0)
        message(FATAL_ERROR "gzip of the Debian changelog failed: ${_gzip_result}")
    endif()
    file(INSTALL "${CPACK_TOPLEVEL_DIRECTORY}/changelog.gz"
         DESTINATION "${CMAKE_INSTALL_PREFIX}/share/doc/${CPACK_PACKAGE_NAME}")
elseif(CPACK_GENERATOR STREQUAL "RPM")
    file(INSTALL "${_source_dir}/LICENSE"
         DESTINATION "${CMAKE_INSTALL_PREFIX}/share/licenses/${CPACK_PACKAGE_NAME}")
else()
    file(INSTALL "${_source_dir}/LICENSE"
         DESTINATION "${CMAKE_INSTALL_PREFIX}/share/doc/${CPACK_PACKAGE_NAME}")
endif()
