#include "lod/status.h"
#include "sc_test.h"

// testing.md §2.1: every Status code is reachable and named.
LOD_TEST(status, names)
{
  LOD_CHECK(lod::StatusName(lod::Status::Ok) == "Ok");
  LOD_CHECK(lod::StatusName(lod::Status::Error) == "Error");
  LOD_CHECK(lod::StatusName(lod::Status::InvalidArgument) == "InvalidArgument");
  LOD_CHECK(lod::StatusName(lod::Status::NotFound) == "NotFound");
  LOD_CHECK(lod::StatusName(lod::Status::NotSupported) == "NotSupported");
  LOD_CHECK(lod::StatusName(lod::Status::OutOfMemory) == "OutOfMemory");
  LOD_CHECK(lod::StatusName(lod::Status::Timeout) == "Timeout");
  LOD_CHECK(lod::StatusName(lod::Status::AlreadyExists) == "AlreadyExists");
  LOD_CHECK(lod::StatusName(lod::Status::WouldBlock) == "WouldBlock");
}

LOD_TEST(status, expected_roundtrip)
{
  lod::Result<int> good = 42;
  lod::Result<int> bad = std::unexpected(lod::Status::NotFound);
  LOD_REQUIRE(good.has_value());
  LOD_CHECK(*good == 42);
  LOD_REQUIRE(!bad.has_value());
  LOD_CHECK(bad.error() == lod::Status::NotFound);

  // Propagation via and_then-style helper (no exceptions involved).
  auto twice = [](int v) -> lod::Result<int> { return v * 2; };
  auto chained = good.and_then(twice);
  LOD_REQUIRE(chained.has_value());
  LOD_CHECK(*chained == 84);
}
