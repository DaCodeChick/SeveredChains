# psx standalone tests (testing.md §1/§2.5/§2.11). Links psx only; self-contained PSX_*
# harness (cpp.md §8.1) so the psx subproject can be extracted to its own repository
# unchanged.
#
# Four targets, because the C17 core has four distinct consumers:
#   sc_tests_psx          C++23 wrapper runner (one case group per format)
#   sc_psx_c_consumer     pure C17 program: proves the C API stands alone
#   sc_tests_psx (clinkage) C++ TU that includes the C header inside extern "C"
#   psx.abi.symbols       exported-symbol audit of the built library

# --- C++ wrapper runner -----------------------------------------------------
add_executable(sc_tests_psx
  test_smoke.cpp
  test_bpe.cpp
  test_tim.cpp
  test_archive.cpp
  test_xa.cpp
  test_wrapper.cpp
  test_c_linkage.cpp)
target_link_libraries(sc_tests_psx PRIVATE psx::cpp)

if(NOT TARGET sc::warn)
  if(MSVC)
    target_compile_options(sc_tests_psx PRIVATE /W4 /GR-)
  else()
    target_compile_options(sc_tests_psx PRIVATE -Wall -Wextra -fno-rtti)
  endif()
else()
  sc_link_policies(sc_tests_psx NORTTI)
endif()

# The psx library is exception-free by contract; so is its test runner, which must
# keep building under either project-wide exception policy.
if(NOT TARGET sc::exceptions_off)
  if(MSVC)
    target_compile_options(sc_tests_psx PRIVATE /EHs-c-)
  else()
    target_compile_options(sc_tests_psx PRIVATE -fno-exceptions)
  endif()
else()
  target_link_libraries(sc_tests_psx PRIVATE sc::exceptions_off)
endif()

# Authentic-asset fixtures stay optional: the suite skips them when the reference
# checkout is absent rather than failing (testing.md §2.2).
if(DEFINED SC_PSX_ASSET_DIR)
  target_compile_definitions(sc_tests_psx PRIVATE SC_PSX_ASSET_DIR="${SC_PSX_ASSET_DIR}")
endif()

# The XA goldens are the opposite case: synthetic input, reference-generated
# expected output, committed to the repository. There is no host condition under
# which they may legitimately be absent, so the path is always defined and
# test_xa.cpp fails rather than skips when it cannot be read.
target_compile_definitions(sc_tests_psx PRIVATE
  PSX_XA_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/../testdata/xa_adpcm.bin")

foreach(group psx bpe tim xa archive wrapper clinkage)
  add_test(NAME psx.${group} COMMAND sc_tests_psx ${group})
endforeach()

# --- pure C17 consumer ------------------------------------------------------
# Compiled by the C compiler against the installed-shape header only. This is the
# target that answers "can a C program use this library?" (psx-c-api.md §8).
add_executable(sc_psx_c_consumer c_consumer/main.c)
set_target_properties(sc_psx_c_consumer PROPERTIES
  C_STANDARD 17
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF
  FOLDER psx/tests)
target_link_libraries(sc_psx_c_consumer PRIVATE psx::psx)

if(MSVC)
  target_compile_options(sc_psx_c_consumer PRIVATE /W4)
else()
  target_compile_options(sc_psx_c_consumer PRIVATE
    -Wall -Wextra -Wshadow -Wconversion -Wmissing-prototypes -Wstrict-prototypes
    -Wpointer-arith -Wwrite-strings -Wredundant-decls -pedantic)
endif()

add_test(NAME psx.c_consumer COMMAND sc_psx_c_consumer)
set_tests_properties(psx.c_consumer PROPERTIES LABELS "psx;c")

# --- exported-symbol audit --------------------------------------------------
# Runs against the library this build actually produced, so a symbol that stopped
# being exported (or a C++ symbol that started leaking) fails the suite.
find_program(PSX_NM NAMES nm llvm-nm)
add_test(NAME psx.abi.symbols
  COMMAND ${CMAKE_COMMAND}
          -D PSX_LIBRARY=$<TARGET_FILE:psx>
          -D NM=${PSX_NM}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/abi_symbols.cmake)
set_tests_properties(psx.abi.symbols PROPERTIES LABELS "psx;abi")
