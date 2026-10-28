# Test framework (testing.md §1): in-repo, dependency-free, ~200-line
# catch-like harness. Macro surface is the sanctioned exception (cpp.md §8.1):
# LOD_TEST / LOD_CHECK / LOD_REQUIRE. Header-only; one main TU per runner.
add_library(sc_test INTERFACE)
add_library(sc::test ALIAS sc_test)
target_include_directories(sc_test INTERFACE .)
target_compile_features(sc_test INTERFACE cxx_std_23)
