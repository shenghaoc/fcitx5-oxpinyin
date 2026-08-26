# libFuzzer harness over the pure input seam (englishNess). Links only the
# helper TU + Fcitx5::Utils — no addon, no engine, no data dirs — so a crash
# or sanitizer hit points at this repository's own logic. Sanitizers are set
# per-target (not via ENABLE_SANITIZER) so fuzz builds stay self-contained.
add_executable(englishness_fuzz englishness_fuzz.cpp
               ${CMAKE_CURRENT_SOURCE_DIR}/../../src/englishness.cpp)
target_include_directories(englishness_fuzz
                           PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
target_link_libraries(englishness_fuzz Fcitx5::Utils)
target_compile_options(englishness_fuzz PRIVATE
                       -fsanitize=fuzzer,address,undefined
                       -fno-sanitize-recover=undefined
                       -fno-omit-frame-pointer)
target_link_options(englishness_fuzz PRIVATE
                    -fsanitize=fuzzer,address,undefined)

# PR-gate smoke: replay the checked-in seed corpus plus a fixed number of
# fresh deterministic runs, with hard time/memory caps. Not a campaign —
# long-running exploration is the nightly workflow's job.
#
# The corpus is COPIED into the build dir first: libFuzzer persists newly
# discovered units into the first corpus directory, and pointing it at the
# source tree would dirty the checkout on every test run.
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/corpus DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
add_test(NAME fuzz-englishness-smoke
         COMMAND englishness_fuzz -runs=2000 -timeout=10 -rss_limit_mb=2560
                 ${CMAKE_CURRENT_BINARY_DIR}/corpus)
