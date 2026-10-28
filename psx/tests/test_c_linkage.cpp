// psx/tests/test_c_linkage.cpp
//
// The C header must be usable from C++ in the way an embedder with an older
// toolchain would include it — inside an explicit extern "C" block — and the symbols it
// declares must resolve against the shared library unchanged (psx-c-api.md §2).
//
// This target is deliberately a C++ translation unit that never touches the C++
// wrappers: if a wrapper type ever leaked into a C header, or a declaration stopped
// being valid C linkage, this file stops compiling or stops linking.

extern "C"
{
#include "psx/psx.h"
}

#include "psx_test.h"

#include <cstdint>
#include <cstring>
#include <type_traits>

PSX_TEST(clinkage, header_layout_and_linkage)
{
  /* Fixed layouts are part of the ABI: a struct that changed size silently would
     break every pre-built consumer. */
  PSX_CHECK(sizeof(psx_bytes_t) == sizeof(void*) + sizeof(std::size_t));
  PSX_CHECK(alignof(psx_bytes_t) == alignof(void*));
  PSX_CHECK(sizeof(psx_tim_rect_t) == 8u);
  PSX_CHECK(sizeof(psx_mrg_entry_t) >= 3u * sizeof(std::size_t) + sizeof(int)); // three sizes + parent
  using ErrorNameResult = decltype(psx_error_name(PSX_OK));
  using ABIResult = std::remove_cv_t<std::remove_reference_t<decltype(psx_abi_version())>>;
  PSX_CHECK((std::is_same_v<ErrorNameResult, const char*>));
  PSX_CHECK((std::is_same_v<ABIResult, std::uint32_t>));

  /* The error type is one enum typedef carrying its own values (not a bare int plus
     loose constants), and it stays int-sized because the header asserts it, so the
     boundary can carry a code the library never produced. That round trip is
     probed at runtime in psx/tests/c_consumer/main.c, which is C: an out-of-range
     enum value is representable there and undefined behaviour here, so the C++ side
     pins only what C++ can express legally. */
  PSX_CHECK((std::is_enum_v<psx_error_t>));
  PSX_CHECK(sizeof(psx_error_t) == sizeof(int));

  /* Calls declared in the header resolve in the shared library. */
  PSX_CHECK(psx_abi_version() == PSX_ABI_VERSION);
  PSX_CHECK(std::strlen(psx_version_string()) > 0u);
  PSX_CHECK(psx_tim_clut_entry_count(psx_tim_rect_t{0, 0, 16, 16}) == 256u);
  PSX_CHECK(!psx_bpe_is_bpe(psx_bytes_t{nullptr, 0u}));
  PSX_CHECK(!psx_archive_is_mrg(psx_bytes_t{nullptr, 0u}));
  PSX_CHECK(!psx_archive_is_deff(psx_bytes_t{nullptr, 0u}));
  PSX_CHECK(!psx_tim_is_tim(psx_bytes_t{nullptr, 0u}));

  /* Handle entry points are reachable and NULL-tolerant as documented. */
  psx_mrg_table_destroy(nullptr);
  psx_deff_table_destroy(nullptr);
  PSX_CHECK(psx_mrg_table_count(nullptr) == 0u);
  PSX_CHECK(psx_deff_table_count(nullptr) == 0u);

  psx_mrg_view_t mrgView{};
  PSX_CHECK(psx_mrg_table_view(nullptr, &mrgView) == PSX_OK);
  PSX_CHECK(mrgView.count == 0u);

  psx_deff_view_t deffView{};
  PSX_CHECK(psx_deff_table_view(nullptr, &deffView) == PSX_OK);
  PSX_CHECK(deffView.count == 0u);

  /* A full decode through the C surface, from a C++ TU. */
  const std::uint8_t segment[] = {0x04u, 0x00u, 0x00u, 0x00u, 'B',   'P',   'E',   0x1au,
                                  0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u};
  std::size_t declared = 0u;
  PSX_CHECK(psx_bpe_declared_size(psx_bytes_t{segment, sizeof(segment)}, &declared) == PSX_OK);
  PSX_CHECK(declared == 4u);

  std::uint8_t out[4] = {0};
  std::size_t written = 0u;
  /* The declared size is never reached, so this is a framing failure rather than a
     truncated byte stream. */
  PSX_CHECK(psx_bpe_decompress(psx_bytes_t{segment, sizeof(segment)}, psx_writable_bytes_t{out, sizeof(out)},
                               &written) == PSX_ERROR_BPE_SIZE_MISMATCH);
}
