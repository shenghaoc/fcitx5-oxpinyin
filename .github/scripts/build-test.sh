#!/bin/bash
# Shared CI configure/build/test step for the container jobs. Per-job
# variation arrives through env: CC/CXX and CMAKE_FLAGS (word-split on
# purpose). CI always builds with warnings-as-errors: the diagnostic layer
# (see root CMakeLists) is probed clean on gcc AND clang, so any new warning
# is a regression, not baseline noise.
set -euo pipefail

# Start clean: reusing a tree configured on another host fails cmake cache
# path validation.
rm -rf build

# CMAKE_FLAGS is deliberately unquoted: the per-job flag list word-splits
# here on purpose.
# shellcheck disable=SC2086
cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE:-Release}" \
    -DOXPINYIN_WARNINGS_AS_ERRORS=ON \
    -DOXPINYIN_USER_DATA_DIR="$(mktemp -d)" \
    ${CMAKE_FLAGS:-}
cmake --build build
ctest --test-dir build --output-on-failure
