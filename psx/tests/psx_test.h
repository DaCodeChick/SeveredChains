#pragma once
/// @file psx_test.h
/// @brief Self-contained PSX_* test harness (cpp.md §8.1 sanctioned macros).
/// Lives inside psx/ so the subproject can be extracted to its own repo
/// unchanged — it must never depend on the engine's test framework.

#include <cstdio>
#include <cstring>
#include <source_location>
#include <string_view>
#include <vector>

namespace psx::test
{

struct Case
{
  const char* name;
  void (*fn)(bool& Ok);
};

inline std::vector<Case>& Registry()
{
  static std::vector<Case> R;
  return R;
}

inline bool Check(bool cond, const char* expr, const std::source_location& loc)
{
  if (!cond)
  {
    std::printf("  FAIL %s:%u: %s\n", loc.file_name(), loc.line(), expr);
    return false;
  }
  return true;
}

/// @brief Run every registered case whose name starts with Filter (empty = all).
/// @param Filter Case-name prefix, e.g. "bpe"; CTest uses one entry per group.
/// @return 0 when every selected case passed, otherwise the failure count.
inline int RunAll(std::string_view Filter = {})
{
  int failedCases = 0;
  int ran = 0;
  for (const Case& tc : Registry())
  {
    if (!Filter.empty() && std::string_view(tc.name).substr(0, Filter.size()) != Filter)
      continue;
    ++ran;
    bool Ok = true;
    std::printf("[ RUN  ] %s\n", tc.name);
    tc.fn(Ok);
    if (!Ok)
    {
      ++failedCases;
      std::printf("[ FAIL ] %s\n", tc.name);
    }
    else
    {
      std::printf("[  OK  ] %s\n", tc.name);
    }
  }
  if (ran == 0)
  {
    // A filter that selects nothing is a test-authoring bug, not a pass.
    std::printf("no cases matched filter '%.*s'\n", static_cast<int>(Filter.size()), Filter.data());
    return 1;
  }
  std::printf("%d case(s) ran, %d failed\n", ran, failedCases);
  return failedCases == 0 ? 0 : 1;
}

struct Registrar
{
  Registrar(const char* name, void (*fn)(bool&)) { Registry().push_back({name, fn}); }
};

} // namespace psx::test

#define PSX_TEST(Group, name)                                                                                          \
  static void PsxTestFn_##Group##_##name(bool& ScOk);                                                                  \
  static ::psx::test::Registrar PsxTestReg_##Group##_##name(#Group "." #name, &PsxTestFn_##Group##_##name);            \
  static void PsxTestFn_##Group##_##name([[maybe_unused]] bool& ScOk)

#define PSX_CHECK(expr) ScOk = ::psx::test::Check((expr), #expr, SC_SOURCE_HERE()) && ScOk

#define SC_SOURCE_HERE() (::std::source_location::current())

#define PSX_TEST_MAIN()                                                                                                \
  int main(int Argc, char** Argv)                                                                                      \
  {                                                                                                                    \
    const std::string_view filter = Argc > 1 ? std::string_view(Argv[1]) : std::string_view();                         \
    return ::psx::test::RunAll(filter);                                                                                \
  }
