# scengine — engine core (namespace lod). Depends on the psx C++ wrappers (which
# bring the C17 core) + SDL3
# (engine-design §1; dependencies.md).
add_library(scengine
  src/log.cpp
  src/diagnostics.cpp
  src/frame.cpp
  src/input.cpp
  src/platform.cpp
  src/fs.cpp
  src/config.cpp
  src/resources.cpp
  src/app.cpp)
add_library(sc::engine ALIAS scengine)
target_include_directories(scengine PUBLIC include)
target_link_libraries(scengine PUBLIC psx::cpp SDL3::SDL3)
sc_link_policies(scengine)
