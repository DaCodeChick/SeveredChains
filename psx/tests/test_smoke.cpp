#include "psx/cpp/psx.hpp"
#include "psx_test.h"

// Standalone smoke: reaching this test proves the psx subproject configured,
// compiled, and linked on its own (testing.md §2.5 zero-dependency audit).
// This TU also owns the suite's main(): PSX_TEST_MAIN is used exactly once per
// runner, so the other psx test files register cases only.
PSX_TEST(psx, standalone_build_and_link)
{
  // A documented constant, not a link probe: it fails if the header surface
  // that consumers rely on stops existing.
  PSX_CHECK(psx::bpe::Magic == 0x1a455042u);
  PSX_CHECK(psx::archive::MRGMagic == 0x1a47524du);
  PSX_CHECK(psx::archive::DEFFMagic == 0x46464544u);
  PSX_CHECK(psx::tim::Magic == 0x10u);
  PSX_CHECK(psx::archive::SectorSize == 0x800u);
}

PSX_TEST(psx, error_names_are_stable_and_complete)
{
  // Crash logs and test output print these; renaming one is a breaking change.
  PSX_CHECK(psx::ErrorName(psx::Error::Truncated) == "Truncated");
  PSX_CHECK(psx::ErrorName(psx::Error::BPEMagic) == "BPEMagic");
  PSX_CHECK(psx::ErrorName(psx::Error::BPEExpansionOverflow) == "BPEExpansionOverflow");
  PSX_CHECK(psx::ErrorName(psx::Error::TIMRectExtent) == "TIMRectExtent");
  PSX_CHECK(psx::ErrorName(psx::Error::MRGEntryOutOfRange) == "MRGEntryOutOfRange");
  PSX_CHECK(psx::ErrorName(psx::Error::DEFFOffsetsBad) == "DEFFOffsetsBad");
  PSX_CHECK(psx::ErrorName(static_cast<psx::Error>(9999)) == "Unknown");
}

PSX_TEST_MAIN()
