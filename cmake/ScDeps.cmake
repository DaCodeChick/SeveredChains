# ScDeps.cmake — third-party dependency resolution (build-system.md §3).
# Strategy: find-package preferred (system/Homebrew/apt/vcpkg), pinned via a
# minimum version requirement; a source-build fallback exists for platforms
# whose package is missing or older than the pinned minimum. Nothing here may
# hard-code a developer machine's paths, and nothing is fetched that no target
# needs. Never fetch a dependency silently: the provider is always reported.

include(FetchContent)

# ---------------------------------------------------------------------------
# SDL3 (dependencies.md: stable >= 3.2.0, zlib) — windowing/input/timing.
#
# Provider selection (build-system.md §3):
#   default            find_package(SDL3 3.2 CONFIG) — brew `sdl3`, apt
#                      `libsdl3-dev`, vcpkg `libsdl3`, or a user-supplied
#                      CMAKE_PREFIX_PATH / SDL3_DIR.
#   SC_DEPS_FETCHCONTENT=ON
#                      build SDL3 from source at a pinned release tag. This is
#                      the path for a platform whose package manager has no
#                      SDL3 (or none new enough); it is what the Windows CI leg
#                      uses, so the fallback stays exercised code rather than a
#                      comment.
#
# The source build is static on purpose: a static SDL3 needs no SDL3.dll next
# to the test executables, which keeps `ctest` from failing at process startup
# for a reason unrelated to the code under test.
# ---------------------------------------------------------------------------
option(SC_DEPS_FETCHCONTENT
  "Build third-party dependencies from source instead of using system packages" OFF)

set(SC_SDL3_VERSION "3.4.18"
  CACHE STRING "Pinned SDL3 source version used by the FetchContent fallback")

set(ScSDL3Provider "")

if(NOT SC_DEPS_FETCHCONTENT)
  find_package(SDL3 3.2.0 CONFIG QUIET)
  if(SDL3_FOUND)
    set(ScSDL3Provider "package (SDL3_DIR=${SDL3_DIR})")
  else()
    message(STATUS
      "SDL3 >= 3.2 was not found as a package. Install it (`brew install sdl3`, "
      "`apt install libsdl3-dev`, vcpkg `libsdl3`) or configure with "
      "-DSC_DEPS_FETCHCONTENT=ON to build SDL3 ${SC_SDL3_VERSION} from source.")
  endif()
endif()

if(NOT SDL3_FOUND)
  if(NOT SC_DEPS_FETCHCONTENT)
    message(FATAL_ERROR
      "No SDL3 provider available. Either install the platform SDL3 package or "
      "configure with -DSC_DEPS_FETCHCONTENT=ON (build-system.md §3).")
  endif()

  message(STATUS "SDL3 provider: source build of ${SC_SDL3_VERSION} (FetchContent, static)")
  # Pinned to a release tag, not a branch (AGENTS.md: pinned, stable versions).
  FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        "release-${SC_SDL3_VERSION}"
    GIT_SHALLOW    TRUE
    SYSTEM         TRUE)   # headers are third-party: no -Werror noise (needs CMake >= 3.25)

  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON  CACHE BOOL "" FORCE)
  set(SDL_TESTS OFF CACHE BOOL "" FORCE)
  set(SDL_TEST  OFF CACHE BOOL "" FORCE)   # spelling used by older SDL3 CMake
  FetchContent_MakeAvailable(SDL3)

  # Assert the provider rather than assume it: every consumer links
  # SDL3::SDL3, and the packaged form always provides that name (it aliases
  # either the shared or the static target). If a future SDL3 stops exporting
  # it, the build must stop here instead of failing at link time on each target.
  if(NOT TARGET SDL3::SDL3)
    message(FATAL_ERROR
      "SDL3 ${SC_SDL3_VERSION} was fetched but defines no SDL3::SDL3 target — "
      "the source-build fallback needs revisiting (see psx-c-api.md/build-system.md §3).")
  endif()
  set(ScSDL3Provider "source (${SC_SDL3_VERSION}, static)")
endif()

message(STATUS "SDL3 provider: ${ScSDL3Provider}")
