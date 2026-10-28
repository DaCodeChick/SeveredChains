# psx — reusable PlayStation 1 library (psx-library.md, psx-c-api.md).
#
# Architecture: a **C17 core** (shared library by default) plus optional
# **header-only C++23 wrappers** in include/psx/cpp. The core must stay usable on
# its own: zero dependencies, no C++ runtime, extractable to its own repository
# unchanged.
cmake_minimum_required(VERSION 3.28)

# Standalone detection must run BEFORE project(): PROJECT_IS_TOP_LEVEL is only
# meaningful after project() has been called.
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  project(psx LANGUAGES C CXX)
  set(CMAKE_C_STANDARD 17)
  set(CMAKE_C_STANDARD_REQUIRED ON)
  set(CMAKE_C_EXTENSIONS OFF)
  set(CMAKE_CXX_STANDARD 23)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
  option(PSX_ENABLE_TESTS "Build psx standalone tests" ON)
  option(PSX_ENABLE_INSTALL "Add psx install/export rules" ON)
else()
  set(PSX_ENABLE_TESTS ${SC_ENABLE_TESTS})
  option(PSX_ENABLE_INSTALL "Add psx install/export rules" ON)
endif()

include(GNUInstallDirs)

# The migration brief pins the core as a shared C library, and the architecture leaves
# the build form to the user (build-system.md §1). Both are honoured: BUILD_SHARED_LIBS
# drives the default when the user sets it, and PSX_SHARED overrides it explicitly.
if(DEFINED BUILD_SHARED_LIBS)
    set(_psx_shared_default ${BUILD_SHARED_LIBS})
else()
    set(_psx_shared_default ON)
endif()
option(PSX_SHARED "Build the psx core as a shared C library" ${_psx_shared_default})
option(PSX_ENABLE_DIFFERENTIAL_TEST
  "Re-run the C17 core against the pre-migration C++ implementation (psx/migration)" OFF)

set(PSX_PUBLIC_HEADERS
  include/psx/archive.h
  include/psx/bpe.h
  include/psx/bytes.h
  include/psx/error.h
  include/psx/export.h
  include/psx/psx.h
  include/psx/tim.h
  include/psx/version.h)

set(PSX_WRAPPER_HEADERS
  include/psx/cpp/archive.hpp
  include/psx/cpp/bpe.hpp
  include/psx/cpp/bytes.hpp
  include/psx/cpp/error.hpp
  include/psx/cpp/psx.hpp
  include/psx/cpp/tim.hpp)

set(PSX_CORE_SOURCES
  src/archive.c
  src/bpe.c
  src/error.c
  src/tim.c
  src/version.c)

# --- library version and ABI version, parsed from psx/version.h -------------
# Single source of truth: the header the compiler sees, not a CMake duplicate.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/include/psx/version.h" psx_version_header)
foreach(component MAJOR MINOR PATCH)
  string(REGEX MATCH "#define PSX_VERSION_${component} ([0-9]+)" _psx_match "${psx_version_header}")
  # Note: test the match, not CMAKE_MATCH_1 — a version component of "0" is a
  # legitimate capture that CMake would read as false.
  if(_psx_match STREQUAL "")
    message(FATAL_ERROR "psx/version.h: could not parse PSX_VERSION_${component}")
  endif()
  set(PSX_VERSION_${component} "${CMAKE_MATCH_1}")
endforeach()
string(REGEX MATCH "#define PSX_ABI_VERSION ([0-9]+)u" _psx_match "${psx_version_header}")
if(_psx_match STREQUAL "")
  message(FATAL_ERROR "psx/version.h: could not parse PSX_ABI_VERSION")
endif()
set(PSX_ABI_VERSION "${CMAKE_MATCH_1}")
set(PSX_LIB_VERSION "${PSX_VERSION_MAJOR}.${PSX_VERSION_MINOR}.${PSX_VERSION_PATCH}")

# --- C17 core ---------------------------------------------------------------
if(PSX_SHARED)
  add_library(psx SHARED ${PSX_CORE_SOURCES} ${PSX_PUBLIC_HEADERS})
else()
  add_library(psx STATIC ${PSX_CORE_SOURCES} ${PSX_PUBLIC_HEADERS})
endif()
add_library(psx::psx ALIAS psx)

target_include_directories(psx PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(psx PUBLIC c_std_17)
set_target_properties(psx PROPERTIES
  C_STANDARD 17
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF
  POSITION_INDEPENDENT_CODE ON)

# Symbol export + hiding (psx-c-api.md §2). Only PSX_API-marked symbols are part
# of the ABI, so internals cannot be reached by accident.
set_target_properties(psx PROPERTIES C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden
                                     VISIBILITY_INLINES_HIDDEN ON)
if(PSX_SHARED)
  target_compile_definitions(psx PRIVATE PSX_BUILDING_SHARED)
  # SONAME carries the ABI version, so an old binary refuses to load an
  # incompatible library instead of misreading a struct.
  set_target_properties(psx PROPERTIES VERSION ${PSX_LIB_VERSION} SOVERSION ${PSX_ABI_VERSION})
else()
  target_compile_definitions(psx PUBLIC PSX_STATIC)
endif()

# Exception/RTTI discipline is a C++ concern for the core; the C core cannot use
# either. Warning policy comes from the shared interface in-tree, and is spelled
# out for standalone builds.
if(TARGET sc::warn)
  target_link_libraries(psx PRIVATE sc::warn)
elseif(MSVC)
  target_compile_options(psx PRIVATE /W4 /permissive-)
else()
  target_compile_options(psx PRIVATE -Wall -Wextra -Wshadow -Wconversion -Wcast-qual -Wmissing-prototypes
                                     -Wstrict-prototypes -Wredundant-decls)
endif()

# Zero-dependency audit (psx-library.md): the core may link nothing except the
# project-internal, flag-carrying policy interfaces. Anything else — third-party
# or engine targets — fails configure.
get_target_property(psx_interface_links psx INTERFACE_LINK_LIBRARIES)
if(psx_interface_links)
  list(FILTER psx_interface_links EXCLUDE REGEX "BUILD_INTERFACE")
  if(psx_interface_links)
    message(FATAL_ERROR "psx must remain zero-dependency; found: ${psx_interface_links}")
  endif()
endif()

# The core must not carry C++ sources: that is what makes it usable from a pure C
# consumer, and what keeps libstdc++/libc++ out of the shared object.
get_target_property(psx_sources psx SOURCES)
if(psx_sources MATCHES "\\.cpp|\\.cc|\\.cxx")
  message(FATAL_ERROR "psx core must be pure C; found C++ source in: ${psx_sources}")
endif()

# --- C++23 wrappers (optional, header-only) ---------------------------------
add_library(psx_cpp INTERFACE)
add_library(psx::cpp ALIAS psx_cpp)
target_include_directories(psx_cpp INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(psx_cpp INTERFACE cxx_std_23)
target_link_libraries(psx_cpp INTERFACE psx::psx)
# The exported name must match the in-tree alias: install(EXPORT ... NAMESPACE psx::)
# derives "psx::psx_cpp" from the target name, which would make an out-of-tree consumer
# write a different name than the engine does. EXPORT_NAME keeps both `psx::cpp`.
set_target_properties(psx_cpp PROPERTIES EXPORT_NAME cpp)
# The wrapper headers ship through the include/psx install rule below, so they need
# no separate install target of their own.

# --- install / export -------------------------------------------------------
if(PSX_ENABLE_INSTALL)
  include(CMakePackageConfigHelpers)

  install(TARGETS psx psx_cpp
    EXPORT psxTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
  install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/psx DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  install(EXPORT psxTargets
    FILE psxTargets.cmake
    NAMESPACE psx::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/psx)
  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/psx-config-version.cmake"
    VERSION ${PSX_LIB_VERSION}
    COMPATIBILITY SameMajorVersion)
  configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/psx-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/psx-config.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/psx)
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/psx-config.cmake"
                "${CMAKE_CURRENT_BINARY_DIR}/psx-config-version.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/psx)
endif()

# --- tests ------------------------------------------------------------------
if(PSX_ENABLE_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# Differential verification links the frozen C++ implementation next to the C17 core,
# so it needs a test runner and therefore a test-enabled build. enable_testing() has to
# precede the add_test() call inside that subdirectory.
if(PSX_ENABLE_DIFFERENTIAL_TEST)
  if(NOT PSX_ENABLE_TESTS)
    message(FATAL_ERROR "PSX_ENABLE_DIFFERENTIAL_TEST requires PSX_ENABLE_TESTS=ON")
  endif()
  enable_testing()
  set(PSX_TEST_HARNESS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests")
  add_subdirectory(migration)
endif()
