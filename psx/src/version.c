/// @file version.c
/// @brief Version reporting. The values come from psx/version.h so a header that
/// disagrees with the loaded library is detectable (psx.c_api asserts both
/// agree; docs/architecture/psx-c-api.md §6 explains the two version axes).

#include "psx/version.h"

uint32_t psx_version_major(void)
{
  return (uint32_t)PSX_VERSION_MAJOR;
}

uint32_t psx_version_minor(void)
{
  return (uint32_t)PSX_VERSION_MINOR;
}

uint32_t psx_version_patch(void)
{
  return (uint32_t)PSX_VERSION_PATCH;
}

uint32_t psx_abi_version(void)
{
  return PSX_ABI_VERSION;
}

const char* psx_version_string(void)
{
  return PSX_VERSION_STRING;
}
