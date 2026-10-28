# lodsc — thin native host executable (build-system.md §1: thin host).
add_executable(lodsc src/main.cpp)
# sc::game chains PUBLIC to sc::engine and psx::psx; no duplicate links.
target_link_libraries(lodsc PRIVATE sc::game)
sc_link_policies(lodsc)
set_target_properties(lodsc PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
