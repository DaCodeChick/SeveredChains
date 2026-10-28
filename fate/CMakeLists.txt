cmake_minimum_required(VERSION 3.25)

# `fate` — Fate Script toolchain, built as one executable.
#
# It is deliberately NOT a library target: the toolchain's public surface is its CLI and
# its file formats, and a shared library without a second consumer would only add an ABI
# to maintain. If a second consumer ever appears, revisit this with a written
# justification rather than quietly extracting the code.
#
# C23 is requested for this target only; the repository-wide default stays C17.

project(fate LANGUAGES C)

set(FATE_C_STANDARD 23)

# Configure-time probe: report a toolchain that cannot do C23 instead of silently
# compiling the sources in an older dialect, which would change `auto` and designated
# initialiser behaviour without any visible error.
#
# The standard has to be raised for the probe, not just appended as a flag. When this
# file is configured as a subdirectory the parent already sets CMAKE_C_STANDARD to 17,
# and CMake then emits `-std=c17` *after* a `CMAKE_REQUIRED_FLAGS` `-std=c23`, so the
# last flag wins and the probe tests C17 while claiming to test C23. Measured: the
# standalone configure passed, the in-tree one failed on that host — i.e. the flag form
# was giving a result for the wrong reason in one of the two cases.
include(CheckCSourceCompiles)

set(_fate_std_saved "${CMAKE_C_STANDARD}")
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_EXTENSIONS OFF)

check_c_source_compiles("
  int main(void) { auto x = 1; return x - 1; }
" FATE_HAS_C23)

# Restore exactly, including "unset": leaving CMAKE_C_STANDARD as an empty string makes
# every later target report an invalid C_STANDARD, so an absent parent default has to go
# back to being absent rather than blank.
if(DEFINED _fate_std_saved AND NOT _fate_std_saved STREQUAL "")
  set(CMAKE_C_STANDARD "${_fate_std_saved}")
else()
  unset(CMAKE_C_STANDARD)
  unset(CMAKE_C_STANDARD CACHE)
endif()
unset(_fate_std_saved)

if(NOT FATE_HAS_C23)
  message(FATAL_ERROR
    "The C compiler could not compile a C23 probe (auto-declaration). "
    "fate is written in C23; refusing to downgrade it silently.")
endif()

add_executable(fate
  src/util.c
  src/lexer.c
  src/parse.c
  src/model.c
  src/meta.c
  src/emit.c
  src/main.c)

target_compile_features(fate PRIVATE c_std_23)
target_compile_options(fate PRIVATE
  $<$<C_COMPILER_ID:Clang,AppleClang,GNU>:
    -std=c23 -Wall -Wextra -Wshadow -Wconversion
    -Wmissing-prototypes -Wstrict-prototypes -pedantic>
  $<$<C_COMPILER_ID:MSVC>:/W4>)

# The tools live beside the sources for the include of optable.h.
target_include_directories(fate PRIVATE src)

set_target_properties(fate PROPERTIES
  C_STANDARD 23
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

enable_testing()

# Differential test against the oracle goldens. Skips loudly (NOT_APPLICABLE) when the
# golden corpus or the metadata bundle is absent, so a missing input never reads as a
# pass.
#
# Default-on. Eighteen of the nineteen fixtures must match the reference byte for byte;
# `p12_forloop` is pinned in manifest.json as an accepted deviation (invalid source,
# where the reference's own output is an ANTLR error-recovery artefact) and is still
# checked for the properties that must hold: diagnosed exit 3, no crash, output differs.
option(FATE_ENABLE_GOLDEN_TEST "Run the fate compile golden-differential test" ON)

find_program(FATE_TEST_PYTHON3 NAMES python3)

if(FATE_ENABLE_GOLDEN_TEST AND FATE_TEST_PYTHON3
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/testdata/manifest.json")
  add_test(NAME fate.compile.golden
    COMMAND ${FATE_TEST_PYTHON3}
            ${CMAKE_CURRENT_SOURCE_DIR}/testdata/run_golden.py
            $<TARGET_FILE:fate>
            ${CMAKE_CURRENT_SOURCE_DIR}/testdata)
  set_tests_properties(fate.compile.golden PROPERTIES
    # 77 is how the harness says "an input this machine does not have". Without
    # this property CTest reads any non-zero exit as a failure, so a runner with no
    # metadata bundle would report a broken compiler rather than an absent fixture.
    SKIP_RETURN_CODE 77
    ENVIRONMENT "FATE_META_DIR=$ENV{FATE_META_DIR}")
endif()
