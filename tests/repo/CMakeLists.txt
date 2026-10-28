# Repository-wide static checks (not domain tests): they inspect source and
# docs, so they run against the *source* tree rather than build artefacts.

# Acronym/naming lint (AGENTS.md naming table, cpp.md §2). Python 3 is a
# development host tool here, not a third-party dependency: it is optional, and
# the check is skipped (not failed) when it is absent.
find_program(ScPython3 NAMES python3 python)
if(ScPython3)
  add_test(NAME repo.naming COMMAND ${ScPython3} ${CMAKE_SOURCE_DIR}/tools/check_naming.py)
  set_tests_properties(repo.naming PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
else()
  message(STATUS "python3 not found: repo.naming lint skipped (run tools/check_naming.py manually)")
endif()

# Formatting gate (cpp.md §6, root .clang-format). It runs clang-format --dry-run over
# every project source, which is what makes the style rules *enforceable* rather than
# advisory — including RemoveBracesLLVM, the project rule that single-statement
# if/else/for/while bodies carry no braces. clang-format is a development host tool, not
# a third-party dependency: when it is missing, or older than 17 (so it cannot see
# RemoveBracesLLVM), the script says so on the same line and falls back to a structural
# brace scan, so an under-tooled runner reports what it could not verify instead of
# passing silently.
if(ScPython3)
  add_test(NAME repo.format COMMAND ${ScPython3} ${CMAKE_SOURCE_DIR}/tools/check_format.py)
  set_tests_properties(repo.format PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
else()
  message(STATUS "python3 not found: repo.format gate skipped (run tools/check_format.py manually)")
endif()
