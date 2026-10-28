#pragma once
/// @file sc_test.h
/// @brief Minimal dependency-free test harness (testing.md §1): static-
/// initializer registration, CHECK (continue) / REQUIRE (early return),
/// name/tag filtering, exit code = failed cases. Checks are bool-based and
/// never throw — failure control flow is an early return.
/// Macros are the sanctioned macro exception (cpp.md §8.1).

#include <cstdio>
#include <source_location>
#include <string>
#include <string_view>
#include <vector>

namespace lod::test
{

struct TestCase
{
  std::string Name;
  void (*fn)(bool& ok);
};

inline std::vector<TestCase>& Registry()
{
  static std::vector<TestCase> reg;
  return reg;
}

struct Registrar
{
  Registrar(std::string_view name, void (*fn)(bool&)) { Registry().push_back({std::string(name), fn}); }
};

inline bool CheckOnce(bool& ok, bool cond, std::string_view expr, const std::source_location& loc)
{
  if (!cond)
  {
    ok = false;
    std::printf("    FAIL %s:%u: %s\n", loc.file_name(), loc.line(), std::string(expr).c_str());
  }
  return cond;
}

/// @brief Run registered cases whose name contains filter ("" = all).
inline int RunAll(std::string_view filter)
{
  std::size_t ran = 0;
  std::size_t failed = 0;
  for (const TestCase& tc : Registry())
  {
    if (!filter.empty() && tc.Name.find(filter) == std::string::npos)
      continue;
    ++ran;
    bool ok = true;
    std::printf("[ RUN  ] %s\n", tc.Name.c_str());
    tc.fn(ok);
    if (!ok)
    {
      ++failed;
      std::printf("[ FAIL ] %s\n", tc.Name.c_str());
    }
    else
    {
      std::printf("[  OK  ] %s\n", tc.Name.c_str());
    }
  }
  std::printf("%zu case(s) ran, %zu failed\n", ran, failed);
  if (ran == 0)
  {
    std::printf("error: no cases matched filter '%s'\n", std::string(filter).c_str());
    return 1;
  }
  return failed == 0 ? 0 : 1;
}

} // namespace lod::test

/// @brief Define and register a test case as "Group.name".
#define LOD_TEST(Group, name)                                                                                          \
  static void ScTestFn_##Group##_##name([[maybe_unused]] bool& ScOk);                                                  \
  static ::lod::test::Registrar ScTestReg_##Group##_##name(#Group "." #name, &ScTestFn_##Group##_##name);              \
  static void ScTestFn_##Group##_##name([[maybe_unused]] bool& ScOk)

/// @brief Non-fatal check: records failure, execution continues.
#define LOD_CHECK(expr) ::lod::test::CheckOnce(ScOk, (expr), #expr, std::source_location::current())

/// @brief Fatal check: records failure and returns from the case.
#define LOD_REQUIRE(expr)                                                                                              \
  if (!::lod::test::CheckOnce(ScOk, (expr), #expr, std::source_location::current()))                                   \
  {                                                                                                                    \
    return;                                                                                                            \
  }

/// @brief Runner main for one domain suite; argv[1] = optional name filter.
#define LOD_TEST_MAIN()                                                                                                \
  int main(int Argc, char** Argv)                                                                                      \
  {                                                                                                                    \
    const std::string_view filter = Argc > 1 ? std::string_view(Argv[1]) : "";                                         \
    return ::lod::test::RunAll(filter);                                                                                \
  }
