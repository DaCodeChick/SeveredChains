# sc_tests_engine — engine domain suite (testing.md §2.1).
add_executable(sc_tests_engine
  test_main.cpp
  test_status.cpp
  test_log.cpp
  test_diagnostics.cpp
  test_fs.cpp
  test_config.cpp
  test_platform.cpp
  test_input.cpp
  test_input_sdl.cpp
  test_app.cpp
  test_app_frame.cpp
  test_resources.cpp)
# sc::game chains PUBLIC to sc::engine and psx::psx; no duplicate links.
target_link_libraries(sc_tests_engine PRIVATE sc::game sc::test)
sc_link_policies(sc_tests_engine)
set_target_properties(sc_tests_engine PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# CTest: one test per named case group for granular reporting (the group name
# is the runner's case-name filter, so it must match the LOD_TEST group prefix).
foreach(group status log diagnostics fs config platform input app.loop app.lifecycle app.frame app.timing app.quit frame.merge res)
  add_test(NAME engine.${group} COMMAND sc_tests_engine ${group})
endforeach()

# Sandbox the host smoke run: never touch the developer's real config/data dirs.
file(TO_NATIVE_PATH "${CMAKE_BINARY_DIR}/smoke-sandbox" sc_smoke_sandbox)
set(sc_smoke_env "SC_CONFIG_DIR=${sc_smoke_sandbox}" "SC_DATA_DIR=${sc_smoke_sandbox}" "SC_RESOURCE_DIR=${sc_smoke_sandbox}")

# The sandbox is wiped before the host tests so "first run creates a config"
# is actually exercised on every CTest run, not only the first.
add_test(NAME host.sandbox_reset COMMAND ${CMAKE_COMMAND} -E rm -rf "${sc_smoke_sandbox}")
set_tests_properties(host.sandbox_reset PROPERTIES FIXTURES_SETUP sc-smoke-sandbox)

# Host smoke tests: the executable must start, honour config, and exit cleanly.
add_test(NAME host.smoke COMMAND lodsc --ticks 5 --no-config-write
         --config "${sc_smoke_sandbox}/smoke-config.txt")
set_tests_properties(host.smoke PROPERTIES
  ENVIRONMENT "${sc_smoke_env}"
  FIXTURES_REQUIRED sc-smoke-sandbox
  PASS_REGULAR_EXPRESSION "main loop completed 5 sim tick")

add_test(NAME host.config_create COMMAND lodsc --ticks 2
         --config "${sc_smoke_sandbox}/created-config.txt")
set_tests_properties(host.config_create PROPERTIES
  ENVIRONMENT "${sc_smoke_env}"
  FIXTURES_REQUIRED sc-smoke-sandbox
  PASS_REGULAR_EXPRESSION "no config at .* using defaults")

# --windowed must switch to a real video driver; on a display-less CI host SDL
# picks a working driver or fails cleanly, so assert only that the mode was
# applied and the run ended successfully.
add_test(NAME host.windowed_flag COMMAND lodsc --windowed --ticks 1 --no-config-write
         --config "${sc_smoke_sandbox}/windowed-config.txt")
set_tests_properties(host.windowed_flag PROPERTIES
  ENVIRONMENT "${sc_smoke_env}"
  FIXTURES_REQUIRED sc-smoke-sandbox
  PASS_REGULAR_EXPRESSION "windowed mode")

add_test(NAME host.bad_args COMMAND lodsc --ticks)
set_tests_properties(host.bad_args PROPERTIES
  ENVIRONMENT "${sc_smoke_env}"
  FIXTURES_REQUIRED sc-smoke-sandbox
  WILL_FAIL TRUE
  FAIL_REGULAR_EXPRESSION "needs a value")
