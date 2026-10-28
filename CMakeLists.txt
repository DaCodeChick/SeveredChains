# Severed Chains Native — root build.
# See docs/architecture/build-system.md (layout, flags, probes) and AGENTS.md.
cmake_minimum_required(VERSION 3.28)

# C is enabled for the reusable psx core (C17 shared library, psx-c-api.md);
# everything else is C++23.
project(SeveredChainsNative
  DESCRIPTION "Native C++23 reimplementation of The Legend of Dragoon: Severed Chains"
  LANGUAGES C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ScToolchain)
include(ScDeps)

option(SC_ENABLE_TESTS "Build automated tests (CTest)" ON)
option(SC_ENABLE_FATE "Build the Fate Script toolchain (requires a C23 compiler)" ON)

# Reusable PSX library (standalone subproject), engine core, game content,
# thin host. Build form (static vs shared) follows BUILD_SHARED_LIBS.
add_subdirectory(psx)
add_subdirectory(src/engine)
add_subdirectory(src/game)
add_subdirectory(src/host)

# The Fate toolchain is a standalone CLI, not engine infrastructure. It asks for C23
# for its own targets only; the repository-wide default above stays C17. A toolchain
# that cannot do C23 stops configuration rather than compiling the same sources in an
# older dialect, so `SC_ENABLE_FATE=OFF` is the explicit way to skip it — there is no
# automatic downgrade and no silent skip.
if(SC_ENABLE_FATE)
  add_subdirectory(fate)
endif()

if(SC_ENABLE_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

include(ScPackaging)
