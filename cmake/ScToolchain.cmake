# ScToolchain.cmake — warning policy, exception/RTTI policy, configure-time
# verification probes (build-system.md §2).
#
# Exception policy (AGENTS.md §4, revised): C++ exceptions are **permitted but
# discouraged**. `std::expected`/error codes stay the primary error path;
# exceptions are justified only at fatal-logic boundaries and when integrating
# third-party code that already throws, and never serve as control flow.
# Therefore project targets compile with exceptions **enabled** by default and
# `SC_ENABLE_EXCEPTIONS=OFF` exists to run the whole tree under a strict
# no-exception build (CI / portability audits). Exception-free-ness remains a
# hard *contract* for the portable/public-API boundaries (psx, plugin SDK) —
# see sc_exceptions_off below. The engine codebase itself throws nowhere.

# ---------------------------------------------------------------------------
# Default build type (build-system.md §6 uses RelWithDebInfo for clean clones)
# ---------------------------------------------------------------------------
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

# ---------------------------------------------------------------------------
# Warning policy (build-system.md §4: -Wall -Wextra; -Werror opt-in for CI;
# MSVC /W4 /permissive-)
# ---------------------------------------------------------------------------
add_library(sc_warn INTERFACE)
add_library(sc::warn ALIAS sc_warn)
if(MSVC)
  target_compile_options(sc_warn INTERFACE /W4 /permissive-)
else()
  target_compile_options(sc_warn INTERFACE
    -Wall -Wextra -Wshadow -Wconversion -Wnon-virtual-dtor -Wcast-align)
endif()
option(SC_WERROR "Treat warnings as errors (CI sets ON)" OFF)
if(SC_WERROR)
  if(MSVC)
    target_compile_options(sc_warn INTERFACE /WX)
  else()
    target_compile_options(sc_warn INTERFACE -Werror)
  endif()
endif()

# ---------------------------------------------------------------------------
# RTTI policy: disabled on public-API targets (AGENTS.md: no RTTI across
# public API boundaries; build-system.md §2 sc_nortti).
# ---------------------------------------------------------------------------
add_library(sc_nortti INTERFACE)
add_library(sc::nortti ALIAS sc_nortti)
if(MSVC)
  target_compile_options(sc_nortti INTERFACE /GR-)
else()
  target_compile_options(sc_nortti INTERFACE -fno-rtti)
endif()

# ---------------------------------------------------------------------------
# Exception policy (error-handling.md §1)
#   ON (default)  project code may use exceptions where justified
#   OFF           -fno-exceptions / /EHs-c- on every project-owned target
# ---------------------------------------------------------------------------
option(SC_ENABLE_EXCEPTIONS "Compile project targets with C++ exceptions enabled" ON)
add_library(sc_exceptions_policy INTERFACE)
add_library(sc::exceptions_policy ALIAS sc_exceptions_policy)
if(NOT SC_ENABLE_EXCEPTIONS)
  if(MSVC)
    target_compile_options(sc_exceptions_policy INTERFACE /EHs-c-)
  else()
    target_compile_options(sc_exceptions_policy INTERFACE -fno-exceptions)
  endif()
endif()

# Always-exception-free interface for the boundaries where being exception-free
# is part of the contract, not a style preference: psx (reusable, extractable,
# must stay portable — psx-library.md §5, cpp.md §9.2) and the plugin SDK
# toolchain (native-api.md §1.6/§8). These ignore SC_ENABLE_EXCEPTIONS.
add_library(sc_exceptions_off INTERFACE)
add_library(sc::exceptions_off ALIAS sc_exceptions_off)
if(MSVC)
  target_compile_options(sc_exceptions_off INTERFACE /EHs-c-)
else()
  target_compile_options(sc_exceptions_off INTERFACE -fno-exceptions)
endif()

# The contract is verified rather than assumed by probe #4 below (next section),
# which compiles `throw 1;` with these flags whatever the project-wide policy says.

# ---------------------------------------------------------------------------
# Sanitizers. Dev-only switches that the CI sanitizer legs use: one macOS
# arm64 and two Linux x86_64 legs pass `-DSC_ENABLE_ASAN=ON
# -DSC_ENABLE_UBSAN=ON` (build-system.md §5/§8), so that lane exercises the
# project's own knob rather than a hand-built CMAKE_C_FLAGS string — the CI
# configure step word-splits its argument list, so a flag string containing
# spaces would never reach the compiler whole. TSAN+ASAN combination rejected.
# ---------------------------------------------------------------------------
option(SC_ENABLE_ASAN "Enable AddressSanitizer (best-effort)" OFF)
option(SC_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(SC_ENABLE_TSAN "Enable ThreadSanitizer" OFF)
if(SC_ENABLE_TSAN AND SC_ENABLE_ASAN)
  message(FATAL_ERROR "TSAN+ASAN combination is rejected (build-system.md §5).")
endif()

# Option name -> compiler spelling. The option names follow the build docs
# (ASAN/UBSAN/TSAN), but Clang/GCC only accept `address`, `undefined` and
# `thread` as -fsanitize values (`-fsanitize=asan` is a hard error outside
# GCC); MSVC currently implements address sanitization only.
set(_sc_san_flags "")
foreach(kind IN ITEMS ASAN UBSAN TSAN)
  if(SC_ENABLE_${kind})
    if(MSVC)
      if(kind STREQUAL "ASAN")
        list(APPEND _sc_san_flags /fsanitize=address)
      else()
        message(FATAL_ERROR "MSVC has no ${kind} sanitizer build (Clang/GCC only).")
      endif()
    elseif(kind STREQUAL "ASAN")
      list(APPEND _sc_san_flags -fsanitize=address)
    elseif(kind STREQUAL "UBSAN")
      list(APPEND _sc_san_flags -fsanitize=undefined)
    else()
      list(APPEND _sc_san_flags -fsanitize=thread)
    endif()
  endif()
endforeach()
if(_sc_san_flags)
  if(NOT MSVC)
    list(APPEND _sc_san_flags -fno-omit-frame-pointer -fno-sanitize-recover=all)
  endif()
  add_compile_options(${_sc_san_flags})
  add_link_options(${_sc_san_flags})
endif()

# ---------------------------------------------------------------------------
# Configure-time probes (build-system.md §2: "Milestone 1 acceptance
# criteria — implement; run at configure time").
# ---------------------------------------------------------------------------
include(CheckCXXSourceCompiles)

# 1. C++23 facilities probe (std::expected + std::print are mandatory
#    per error-handling.md §2 and cpp.md §3).
check_cxx_source_compiles("
#include <expected>
#include <print>
int main() { std::println(\"{}\", 1); return 0; }"
  sc_probe_cxx23_facilities)
if(NOT sc_probe_cxx23_facilities)
  message(FATAL_ERROR "Toolchain lacks C++23 std::expected/std::print; "
                      "required by error-handling.md §2 and cpp.md §3.")
endif()

set(_sc_probe_dir "${CMAKE_BINARY_DIR}/sc-probes")
file(MAKE_DIRECTORY "${_sc_probe_dir}")
file(READ "${CMAKE_CURRENT_LIST_DIR}/probes/rtti_probe.cpp" _sc_rtti_probe_src)
file(WRITE "${_sc_probe_dir}/rtti_probe.cpp" "${_sc_rtti_probe_src}")

# 2. RTTI probe: rtti_probe.cpp #errors unless no-RTTI is in effect, so it must
#    compile under the sc_nortti flags (build-system.md §2). It is paired with a
#    run of the same source *without* the flags: a guard that fires in neither
#    configuration proves nothing, and the pair is what distinguishes "flag
#    works" from "this compiler never defines the macro the guard tests".
#    Measured on this machine: with RTTI on the guard fires (AppleClang
#    __GXX_RTTI, and _CPPRTTI predefined in clang's msvc mode), with
#    -fno-rtti / /GR- it does not.
if(MSVC)
  set(_sc_nortti_probe_flag /GR-)
else()
  set(_sc_nortti_probe_flag -fno-rtti)
endif()
set(CMAKE_REQUIRED_FLAGS "${_sc_nortti_probe_flag}")
check_cxx_source_compiles("${_sc_rtti_probe_src}" sc_probe_rtti_off)
set(CMAKE_REQUIRED_FLAGS "")
check_cxx_source_compiles("${_sc_rtti_probe_src}" sc_probe_rtti_on)
if(NOT sc_probe_rtti_off)
  message(FATAL_ERROR
    "RTTI probe failed to compile under '${_sc_nortti_probe_flag}' — RTTI is still "
    "enabled, so the no-RTTI flags are being ignored rather than honoured. The "
    "no-RTTI contract on the public API / plugin SDK surface would be silently "
    "unapplied (build-system.md §2).")
elseif(sc_probe_rtti_on)
  message(STATUS
    "RTTI probe: no RTTI macro was reported with or without "
    "'${_sc_nortti_probe_flag}', so this compiler gives no observable signal that "
    "the flag took effect. Reported rather than assumed — sc_nortti is still "
    "applied to the targets that require it.")
endif()

# 3/4. Exception-policy probes. "Did the no-exceptions flag take effect?" is
# answered by two translation units, because the two compiler families expose
# different observable behaviour:
#
#   probes/throw_probe.cpp      throws. `-fno-exceptions` rejects this on GCC
#                               and Clang, and clang-cl rejects it as well
#                               because it maps /EHs-c- onto -fno-exceptions.
#   probes/eh_macro_probe.cpp   fails unless the compiler reports exception
#                               handling as off. This is the MSVC signal: cl.exe
#                               documents /EH as describing how to clean up
#                               *after* a throw, not whether one is allowed, and
#                               keeps accepting try/throw/catch under /EHs-c-.
#                               Demanding rejection there fails a correctly
#                               working compiler — on every Windows leg, since
#                               probe 4 runs whatever SC_ENABLE_EXCEPTIONS says.
#
# Checked locally against a cl-compatible driver (clang --driver-mode=cl):
# /EHsc fires the macro guard, /EHs-c- does not, so the check is not vacuous.
# Real cl.exe is not available on this machine: if it behaves differently the
# outcome is named in the configure log instead of being assumed either way.
function(ScProbeExceptionPolicy flagVar outVar)
  set(_flags "${${flagVar}}")
  set(_throw_rejected FALSE)

  if(_flags)
    set(CMAKE_REQUIRED_FLAGS "${_flags}")
  endif()
  check_cxx_source_compiles("${_sc_throw_probe_src}" "${outVar}_throw")
  set(CMAKE_REQUIRED_FLAGS "")
  if(NOT "${${outVar}_throw}")
    set(_throw_rejected TRUE)
  endif()

  if(_flags)
    set(CMAKE_REQUIRED_FLAGS "${_flags}")
  endif()
  check_cxx_source_compiles("${_sc_eh_macro_src}" "${outVar}_macro_off")
  set(CMAKE_REQUIRED_FLAGS "")
  check_cxx_source_compiles("${_sc_eh_macro_src}" "${outVar}_macro_on")

  if(_throw_rejected)
    set(_outcome enforced)
  elseif(MSVC AND "${${outVar}_macro_off}" AND NOT "${${outVar}_macro_on}")
    # Flag on => exception model off, flag off => exception model on. The flag
    # is doing something; it just cannot forbid a throw on this compiler.
    set(_outcome macro-verified)
  else()
    set(_outcome unverifiable)
  endif()
  set(${outVar} "${_outcome}" PARENT_SCOPE)
endfunction()

# On GCC/Clang a flag that changes nothing is a dropped flag, and that is the
# failure the probes exist to catch. On MSVC no flag forbids a throw, so the
# same result means "not compiler-enforceable here" and is reported as such:
# neither a false pass nor a false failure.
function(ScReportExceptionPolicy outcome flags context)
  if("${outcome}" STREQUAL "enforced")
    return()
  endif()
  if(MSVC)
    if("${outcome}" STREQUAL "macro-verified")
      message(STATUS
        "Exception probe (${context}): '${flags}' turns off _CPPUNWIND, which is "
        "as far as cl.exe goes — it compiles `throw` either way, so this is "
        "verified by the exception-model macro and not by rejection.")
    else()
      message(STATUS
        "Exception probe (${context}): '${flags}' changed nothing observable on "
        "${CMAKE_CXX_COMPILER_ID} (${outcome}). ${context} is not "
        "compiler-enforced here and is reported rather than assumed; "
        "build-system.md §2.")
    endif()
  else()
    message(FATAL_ERROR
      "Exception probe (${context}): `throw` compiles even with '${flags}'. On "
      "${CMAKE_CXX_COMPILER_ID} that flag does remove exceptions, so it is being "
      "ignored rather than honoured (${context}).")
  endif()
endfunction()

file(READ "${CMAKE_CURRENT_LIST_DIR}/probes/throw_probe.cpp" _sc_throw_probe_src)
file(READ "${CMAKE_CURRENT_LIST_DIR}/probes/eh_macro_probe.cpp" _sc_eh_macro_src)
file(WRITE "${_sc_probe_dir}/throw_probe.cpp" "${_sc_throw_probe_src}")
file(WRITE "${_sc_probe_dir}/eh_macro_probe.cpp" "${_sc_eh_macro_src}")

# 3. Exception-policy probe (only meaningful when the policy is OFF).
if(NOT SC_ENABLE_EXCEPTIONS)
  if(MSVC)
    set(_sc_noexc_probe_flag "/EHs-c-")
  else()
    set(_sc_noexc_probe_flag "-fno-exceptions")
  endif()
  ScProbeExceptionPolicy(_sc_noexc_probe_flag probe_noexc_policy)
  ScReportExceptionPolicy("${probe_noexc_policy}" "${_sc_noexc_probe_flag}"
    "the SC_ENABLE_EXCEPTIONS=OFF policy")
endif()

# 4. Exception-free-boundary probe: `psx` and the plugin SDK are exception-free
#    by contract, so the same checks run whatever SC_ENABLE_EXCEPTIONS says
#    (psx-library.md §5, native-api.md §1.6).
if(MSVC)
  set(_sc_exceptions_off_probe_flag "/EHs-c-")
else()
  set(_sc_exceptions_off_probe_flag "-fno-exceptions")
endif()
ScProbeExceptionPolicy(_sc_exceptions_off_probe_flag probe_noexc_boundary)
ScReportExceptionPolicy("${probe_noexc_boundary}" "${_sc_exceptions_off_probe_flag}"
  "the exception-free psx/plugin-SDK contract")

# 5. Non-vacuity guard for probes 3/4: the same throw_probe.cpp source must
#    compile when the flags are absent, otherwise "failed to compile" would
#    prove nothing. This also catches a toolchain that ignores the flags
#    entirely. One source for both halves, so the pair cannot drift apart.
set(CMAKE_REQUIRED_FLAGS "")
check_cxx_source_compiles("${_sc_throw_probe_src}"
  sc_probe_throw_compiles_baseline)
if(NOT sc_probe_throw_compiles_baseline)
  message(FATAL_ERROR "Baseline `throw` probe failed to compile without any "
                      "policy flags — probes 3/4 cannot verify anything on this "
                      "toolchain and must not be trusted.")
endif()
# ---------------------------------------------------------------------------
# sc_link_policies — per-target warning/policy discipline (build-system.md §2).
# Usage: sc_link_policies(<target> [NORTTI])
# ---------------------------------------------------------------------------
function(sc_link_policies target)
  cmake_parse_arguments(ARG "NORTTI" "" "" ${ARGN})
  target_link_libraries(${target} PRIVATE sc::warn sc::exceptions_policy)
  if(ARG_NORTTI)
    target_link_libraries(${target} PRIVATE sc::nortti)
  endif()
endfunction()
