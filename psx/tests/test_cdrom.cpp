/// @file test_cdrom.cpp
/// @brief CD-ROM sector layer tests (psx::cdrom).
///
/// These are contract tests, not golden tests: there is no disc image in the
/// repository and none is available, so the expected behaviour is taken from
/// the reference *source* (SectorHeader.java, IsoReader.java, XaAdpcm.java) and
/// pinned here. Two consequences worth stating plainly:
///
///  - The synthetic sectors below are built byte-by-byte from that source, so
///    they prove the implementation matches the reference's *bit arithmetic*,
///    not that any real LoD disc decodes. Byte provenance — which positions are
///    corroborated by a second call site and which are only reproduced — is
///    recorded in docs/research/psx-formats.md §8.
///  - Every case is written so a wrong offset or a wrong mask makes it fail.
///    The payload bytes carry their own index, the classify case sweeps all 256
///    submode values, and the run-extent cases include the overflow argument.
///    Changing an offset in cdrom.c to a nearby wrong value breaks at least one
///    case, which is the property to keep when these tests are extended.

#include "psx/cpp/psx.hpp"
#include "psx_test.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace
{

using Bytes = std::vector<std::uint8_t>;

/// @brief Build a synthetic raw sector.
///
/// The sync pattern is the real one (00 FF x9 00) so that a test can tell "the
/// parser ignored the sync" from "the parser was off by twelve". Payload bytes
/// encode their own offset, so any off-by-N read shows up as different content
/// rather than as a coincidental match.
Bytes MakeSector(std::uint8_t submode, std::uint8_t coding)
{
  Bytes sector(PSX_CDROM_SECTOR_SIZE, 0u);
  for (std::size_t i = 1; i <= 10u; ++i)
    sector[i] = 0xFFu;
  sector[PSX_CDROM_SUBMODE_OFFSET] = submode;
  sector[PSX_CDROM_CODING_OFFSET] = coding;
  for (std::size_t i = PSX_CDROM_HEADER_SIZE; i < sector.size(); ++i)
    sector[i] = static_cast<std::uint8_t>(i & 0xFFu);
  return sector;
}

/// @brief Span over a whole vector, for calling the span-taking API.
/// Not a template: nothing here needs the length at compile time, and an
/// unused template parameter is a warning the CI build treats as an error.
std::span<const std::uint8_t> Whole(const Bytes& sector)
{
  return std::span<const std::uint8_t>(sector.data(), sector.size());
}

} // namespace

PSX_TEST(cdrom, header_fields_sit_where_the_reference_puts_them)
{
  const Bytes sector = MakeSector(0x82u, 0x01u);
  const auto header = psx::cdrom::ParseHeader(Whole(sector));
  PSX_CHECK(header.has_value());
  PSX_CHECK(header->submode == 0x82u);
  PSX_CHECK(header->coding == 0x01u);

  // The reference also defines getMode() at byte 15 and never calls it (its own
  // declaration is the only match in the reference repo), so there is no
  // behaviour to reproduce here and no mode field to be wrong about. Changing
  // bytes 15 and 16 must therefore change nothing: if a future change starts
  // reading byte 15 as mode, or starts asserting a mode field exists, this
  // case fails. Whether byte 15 is a mode byte on real hardware is a separate
  // open question (psx-formats.md §8), not something this test settles.
  Bytes changed = sector;
  changed[15] = 0x41u;
  changed[16] = 0x02u;
  const auto again = psx::cdrom::ParseHeader(Whole(changed));
  PSX_CHECK(again.has_value());
  PSX_CHECK(again->channel == header->channel);
  PSX_CHECK(again->submode == header->submode);
  PSX_CHECK(again->coding == header->coding);
}

PSX_TEST(cdrom, header_rejects_short_spans_and_bad_arguments)
{
  const Bytes sector = MakeSector(0x08u, 0x00u);

  // 23 bytes cannot hold the header region; 24 can, even with no payload.
  const auto tooShort =
    psx::cdrom::ParseHeader(std::span<const std::uint8_t>(sector.data(), PSX_CDROM_HEADER_SIZE - 1u));
  PSX_CHECK(!tooShort.has_value());
  PSX_CHECK(tooShort.error() == psx::Error::Truncated);

  Bytes headerOnly(PSX_CDROM_HEADER_SIZE, 0u);
  headerOnly[PSX_CDROM_SUBMODE_OFFSET] = 0x08u;
  const auto justHeader = psx::cdrom::ParseHeader(Whole(headerOnly));
  PSX_CHECK(justHeader.has_value());
  PSX_CHECK(justHeader->submode == 0x08u);

  // A null base with a non-zero size is an invalid argument, not an empty
  // buffer (psx-c-api.md §3). An empty span is a *short* one.
  psx_sector_header_t out{};
  const psx_bytes_t nullWithSize{nullptr, PSX_CDROM_SECTOR_SIZE};
  PSX_CHECK(psx_sector_header_parse(nullWithSize, &out) == PSX_ERROR_INVALID_ARGUMENT);
  const psx_bytes_t empty{nullptr, 0u};
  PSX_CHECK(psx_sector_header_parse(empty, &out) == PSX_ERROR_TRUNCATED);
  PSX_CHECK(psx_sector_header_parse({sector.data(), sector.size()}, nullptr) == PSX_ERROR_INVALID_ARGUMENT);
}

PSX_TEST(cdrom, the_form_bit_selects_the_payload_size)
{
  // Form 1 = DATA sectors as the reference stores them; Form 2 = XA audio.
  const Bytes form1 = MakeSector(0x08u, 0x00u); // submode bit 5 clear
  const Bytes form2 = MakeSector(0x28u, 0x01u); // submode bit 5 set

  const auto small = psx::cdrom::Payload(Whole(form1));
  const auto large = psx::cdrom::Payload(Whole(form2));
  PSX_CHECK(small.has_value());
  PSX_CHECK(large.has_value());
  PSX_CHECK(small->size() == PSX_CDROM_FORM1_DATA_SIZE);
  PSX_CHECK(large->size() == PSX_CDROM_FORM2_DATA_SIZE);

  // Both payloads start at byte 24, and the content check proves it: payload
  // byte i holds the raw sector index it came from.
  PSX_CHECK(small->data() == form1.data() + PSX_CDROM_HEADER_SIZE);
  PSX_CHECK(large->data() == form2.data() + PSX_CDROM_HEADER_SIZE);
  PSX_CHECK((*small)[0] == form1[PSX_CDROM_HEADER_SIZE]);
  PSX_CHECK((*large)[0] == form2[PSX_CDROM_HEADER_SIZE]);
  PSX_CHECK((*large)[PSX_CDROM_FORM2_DATA_SIZE - 1u] == form2[PSX_CDROM_HEADER_SIZE + PSX_CDROM_FORM2_DATA_SIZE - 1u]);

  // The two sizes differ by 280 bytes. A reader that hard-coded 2048 would be
  // quietly wrong for every Form 2 sector; that is the reason the size comes
  // from the sector rather than from a constant.
  PSX_CHECK(PSX_CDROM_FORM2_DATA_SIZE - PSX_CDROM_FORM1_DATA_SIZE == 280u);

  const auto formOf = [](std::uint8_t submode)
  {
    psx_sector_header_t header{};
    header.submode = submode;
    return psx_sector_header_form(header);
  };
  PSX_CHECK(formOf(0x00u) == PSX_SECTOR_FORM_1);
  PSX_CHECK(formOf(0x20u) == PSX_SECTOR_FORM_2);
  PSX_CHECK(formOf(0xFFu) == PSX_SECTOR_FORM_2);
}

PSX_TEST(cdrom, payload_requires_exactly_one_sector)
{
  const Bytes sector = MakeSector(0x08u, 0x00u);

  // Too short: truncation, reported rather than answered with a short payload.
  const auto shortSpan = psx::cdrom::Payload(std::span<const std::uint8_t>(sector.data(), PSX_CDROM_SECTOR_SIZE - 1u));
  PSX_CHECK(!shortSpan.has_value());
  PSX_CHECK(shortSpan.error() == psx::Error::Truncated);

  // Too long: a whole image passed where one sector was meant is a caller bug.
  // Answering it by reading the front would decode sector 0 forever and look
  // like a working pipeline that produces nothing.
  const Bytes twoSectors(PSX_CDROM_SECTOR_SIZE * 2u, 0u);
  const auto tooLong = psx::cdrom::Payload(Whole(twoSectors));
  PSX_CHECK(!tooLong.has_value());
  PSX_CHECK(tooLong.error() == psx::Error::InvalidArgument);

  const auto exact = psx::cdrom::Payload(Whole(sector));
  PSX_CHECK(exact.has_value());
  PSX_CHECK(psx::cdrom::Payload({}).error() == psx::Error::Truncated);
}

PSX_TEST(cdrom, classify_accepts_three_types_and_rejects_the_other_five)
{
  int accepted = 0;
  int rejected = 0;

  // Every submode value, not a sample of them: bits 1..3 give eight patterns
  // and five of them are undefined. The reference throws on those, which is the
  // behaviour this replaces.
  for (unsigned value = 0; value < 256u; ++value)
  {
    psx_sector_header_t header{};
    header.submode = static_cast<std::uint8_t>(value);

    psx_sector_kind_t kind = PSX_SECTOR_KIND_VIDEO;
    const psx_error_t code = psx_sector_classify(header, &kind);
    switch (value & 0x0Eu)
    {
      case 0x02u:
        PSX_CHECK(code == PSX_OK && kind == PSX_SECTOR_KIND_VIDEO);
        ++accepted;
        break;
      case 0x04u:
        PSX_CHECK(code == PSX_OK && kind == PSX_SECTOR_KIND_AUDIO);
        ++accepted;
        break;
      case 0x08u:
        PSX_CHECK(code == PSX_OK && kind == PSX_SECTOR_KIND_DATA);
        ++accepted;
        break;
      default:
        PSX_CHECK(code == PSX_ERROR_CDROM_BAD_SECTOR_TYPE);
        // The output is left untouched on failure, so a caller cannot read a
        // half-trusted classification out of a rejected sector.
        PSX_CHECK(kind == PSX_SECTOR_KIND_VIDEO);
        ++rejected;
        break;
    }
  }

  PSX_CHECK(accepted == 96);
  PSX_CHECK(rejected == 160);

  // NULL destination is an argument error even for a valid type.
  psx_sector_header_t valid{};
  valid.submode = 0x08u;
  PSX_CHECK(psx_sector_classify(valid, nullptr) == PSX_ERROR_INVALID_ARGUMENT);
}

PSX_TEST(cdrom, flags_and_types_do_not_shadow_each_other)
{
  // 0x82: EOF set, type = VIDEO. The reference checks EOF first and stops the
  // whole playback on it, so EOF must not be readable as "a video sector to
  // decode" and nothing else.
  psx_sector_header_t eofSector{};
  eofSector.submode = 0x82u;
  PSX_CHECK(psx_sector_header_is_eof(eofSector) != 0);
  psx_sector_kind_t kind = PSX_SECTOR_KIND_DATA;
  PSX_CHECK(psx_sector_classify(eofSector, &kind) == PSX_OK);
  PSX_CHECK(kind == PSX_SECTOR_KIND_VIDEO);

  // 0x01: end-audio only, type bits clear. A real sector always has a type, and
  // this one does not — rejected, as in the reference.
  psx_sector_header_t noType{};
  noType.submode = 0x01u;
  PSX_CHECK(psx_sector_header_is_end_audio(noType) != 0);
  kind = PSX_SECTOR_KIND_DATA;
  PSX_CHECK(psx_sector_classify(noType, &kind) == PSX_ERROR_CDROM_BAD_SECTOR_TYPE);
  PSX_CHECK(kind == PSX_SECTOR_KIND_DATA);

  // The rest of the flags, each in isolation, so a mask that drifted one bit
  // would be caught here.
  psx_sector_header_t realtime{};
  realtime.submode = 0x40u;
  PSX_CHECK(psx_sector_header_is_realtime(realtime) != 0);
  PSX_CHECK(psx_sector_header_is_trigger(realtime) == 0);

  psx_sector_header_t trigger{};
  trigger.submode = 0x10u;
  PSX_CHECK(psx_sector_header_is_trigger(trigger) != 0);
  PSX_CHECK(psx_sector_header_is_realtime(trigger) == 0);

  psx_sector_header_t everything{};
  everything.submode = 0xFFu;
  PSX_CHECK(psx_sector_header_is_eof(everything) != 0);
  PSX_CHECK(psx_sector_header_is_realtime(everything) != 0);
  PSX_CHECK(psx_sector_header_is_trigger(everything) != 0);
  PSX_CHECK(psx_sector_header_is_end_audio(everything) != 0);
  // 0xFF has type bits 0x0E, which is not one of the three defined types.
  kind = PSX_SECTOR_KIND_DATA;
  PSX_CHECK(psx_sector_classify(everything, &kind) == PSX_ERROR_CDROM_BAD_SECTOR_TYPE);
}

PSX_TEST(cdrom, the_coding_byte_decodes_into_the_reference_matrix)
{
  // SectorHeader.Coding: bit 4 width, bit 2 rate, bit 0 stereo. All 256 values,
  // because a loop over 0..7 would never set bit 4 and so could not tell
  // "8-bit when 0x10 is set" from "always 4-bit".
  for (unsigned coding = 0; coding < 256u; ++coding)
  {
    psx_sector_header_t header{};
    header.coding = static_cast<std::uint8_t>(coding);
    PSX_CHECK(psx_sector_header_coding_bits(header) == ((coding & 0x10u) != 0u ? 8 : 4));
    PSX_CHECK(psx_sector_header_coding_rate(header) == ((coding & 0x04u) != 0u ? 18900 : 37800));
    PSX_CHECK((psx_sector_header_coding_is_stereo(header) != 0) == ((coding & 0x01u) != 0u));
  }

  // The bits the reference reads are 0x10 / 0x04 / 0x01 — not the contiguous
  // 0x10 / 0x08 / 0x01 a hand-written decoder would guess. Pinning the values
  // as well as the use is what keeps a later "tidy-up" from aliasing bit 3.
  PSX_CHECK(PSX_CDROM_CODING_8BIT == 0x10u);
  PSX_CHECK(PSX_CDROM_CODING_HALF_RATE == 0x04u);
  PSX_CHECK(PSX_CDROM_CODING_STEREO == 0x01u);

  // Byte 18 is the submode and byte 19 the coding info. The reference reads
  // data[19]; the standard CD-ROM XA layout says 18 because it counts a
  // three-byte MSF where the reference's buffer has four. Both headers must
  // name the same byte or the sector layer and the decoder read different
  // bytes in silence, so the cross-check is pinned as well as the values.
  PSX_CHECK(PSX_CDROM_SUBMODE_OFFSET == 18u);
  PSX_CHECK(PSX_CDROM_CODING_OFFSET == 19u);
  PSX_CHECK(PSX_CDROM_CODING_OFFSET == PSX_XA_CODING_INFO_OFFSET);
  PSX_CHECK(PSX_CDROM_SECTOR_SIZE == PSX_XA_SECTOR_SIZE);
  PSX_CHECK(PSX_CDROM_HEADER_SIZE == PSX_XA_HEADER_SIZE);
}

PSX_TEST(cdrom, a_form_1_payload_cannot_feed_the_xa_decoder)
{
  // The decoder reads 18 x 128 = 2304 bytes beginning at byte 24 (XaAdpcm's
  // block loop, and the reason the reference's audio path is handed the whole
  // 2352-byte sector). Only a Form 2 sector's data region is wide enough.
  //
  // This case exists to keep that relationship from being lost: if the payload
  // rule were changed to a flat 2048, the two decodes below change outcome and
  // this test fails instead of quietly agreeing.
  constexpr std::size_t DecoderReadSize = PSX_XA_PORTION_COUNT * PSX_XA_PORTION_SIZE;
  PSX_CHECK(DecoderReadSize == 2304u);

  const Bytes form1Audio = MakeSector(0x04u, 0x01u); // AUDIO, Form 1
  const Bytes form2Audio = MakeSector(0x24u, 0x01u); // AUDIO, Form 2

  psx_sector_header_t header1{};
  psx_sector_header_t header2{};
  PSX_CHECK(psx_sector_header_parse({form1Audio.data(), form1Audio.size()}, &header1) == PSX_OK);
  PSX_CHECK(psx_sector_header_parse({form2Audio.data(), form2Audio.size()}, &header2) == PSX_OK);

  psx_sector_kind_t kind = PSX_SECTOR_KIND_DATA;
  PSX_CHECK(psx_sector_classify(header1, &kind) == PSX_OK);
  PSX_CHECK(kind == PSX_SECTOR_KIND_AUDIO);
  PSX_CHECK(psx_sector_header_coding_is_stereo(header2) != 0);

  const auto narrow = psx::cdrom::Payload(Whole(form1Audio));
  const auto wide = psx::cdrom::Payload(Whole(form2Audio));
  PSX_CHECK(narrow.has_value() && narrow->size() < DecoderReadSize);
  PSX_CHECK(wide.has_value() && wide->size() >= DecoderReadSize);

  // Whole-sector decode works for both, which is what Fmv.play() does: it passes
  // the 2352-byte buffer, not an extracted payload.
  Bytes pcm(PSX_XA_MAX_SAMPLES * 2u, 0u);
  psx::xa::Stream stream{};
  psx::xa::Stream other{};
  const auto whole =
    psx::xa::DecodeSector(stream, Whole(form1Audio), 0x01u, std::span<std::uint8_t>(pcm.data(), pcm.size()));
  PSX_CHECK(whole.has_value());
  PSX_CHECK(whole.value() == 4032u);

  // Feeding the extracted payload instead of the sector is the wrong shape for
  // the decoder API and must be reported, not decoded into noise.
  const auto fromNarrow =
    psx::xa::DecodeSector(other, narrow.value(), 0x01u, std::span<std::uint8_t>(pcm.data(), pcm.size()));
  PSX_CHECK(!fromNarrow.has_value());
  PSX_CHECK(fromNarrow.error() == psx::Error::Truncated);
}

PSX_TEST(cdrom, run_extent_covers_consecutive_sectors)
{
  Bytes image(PSX_CDROM_SECTOR_SIZE * 3u, 0u);
  const psx_bytes_t span{image.data(), image.size()};

  psx_sector_extent_t extent{};
  PSX_CHECK(psx_sector_run_extent(span, 0u, 3u, &extent) == PSX_OK);
  PSX_CHECK(extent.offset == 0u);
  PSX_CHECK(extent.length == PSX_CDROM_SECTOR_SIZE * 3u);

  // A mid-image run: an FMV frame is 5 sectors at normal speed, 10 at double.
  PSX_CHECK(psx_sector_run_extent(span, 1u, 2u, &extent) == PSX_OK);
  PSX_CHECK(extent.offset == PSX_CDROM_SECTOR_SIZE);
  PSX_CHECK(extent.length == PSX_CDROM_SECTOR_SIZE * 2u);

  // Last sector reachable; one past it is not.
  PSX_CHECK(psx_sector_run_extent(span, 2u, 1u, &extent) == PSX_OK);
  PSX_CHECK(extent.offset == PSX_CDROM_SECTOR_SIZE * 2u);
  PSX_CHECK(psx_sector_run_extent(span, 2u, 2u, &extent) == PSX_ERROR_TRUNCATED);
  PSX_CHECK(psx_sector_run_extent(span, 3u, 1u, &extent) == PSX_ERROR_TRUNCATED);
  PSX_CHECK(psx_sector_run_extent(span, 999u, 1u, &extent) == PSX_ERROR_TRUNCATED);

  // Zero sectors is an argument error; so is arithmetic that would wrap.
  PSX_CHECK(psx_sector_run_extent(span, 0u, 0u, &extent) == PSX_ERROR_INVALID_ARGUMENT);
  PSX_CHECK(psx_sector_run_extent(span, 0u, SIZE_MAX, &extent) == PSX_ERROR_INVALID_ARGUMENT);
  PSX_CHECK(psx_sector_run_extent(span, SIZE_MAX, 1u, &extent) == PSX_ERROR_INVALID_ARGUMENT);

  // A five-sector frame does not fit in four sectors, and does in five.
  Bytes four(PSX_CDROM_SECTOR_SIZE * 4u, 0u);
  Bytes five(PSX_CDROM_SECTOR_SIZE * 5u, 0u);
  const psx_bytes_t fourSpan{four.data(), four.size()};
  const psx_bytes_t fiveSpan{five.data(), five.size()};
  PSX_CHECK(psx_sector_run_extent(fourSpan, 0u, 5u, &extent) == PSX_ERROR_TRUNCATED);
  PSX_CHECK(psx_sector_run_extent(fiveSpan, 0u, 5u, &extent) == PSX_OK);
  PSX_CHECK(extent.length == PSX_CDROM_SECTOR_SIZE * 5u);

  // NULL with a size, and a NULL destination.
  const psx_bytes_t nullSpan{nullptr, PSX_CDROM_SECTOR_SIZE};
  PSX_CHECK(psx_sector_run_extent(nullSpan, 0u, 1u, &extent) == PSX_ERROR_INVALID_ARGUMENT);
  PSX_CHECK(psx_sector_run_extent(span, 0u, 1u, nullptr) == PSX_ERROR_INVALID_ARGUMENT);
}

PSX_TEST(cdrom, sector_count_uses_the_reference_divisor)
{
  // ceil(length / 2048), verified at the boundaries where an off-by-one shows.
  PSX_CHECK(psx_sector_count_for_bytes(0u) == 0u);
  PSX_CHECK(psx_sector_count_for_bytes(1u) == 1u);
  PSX_CHECK(psx_sector_count_for_bytes(2048u) == 1u);
  PSX_CHECK(psx_sector_count_for_bytes(2049u) == 2u);
  PSX_CHECK(psx_sector_count_for_bytes(4096u) == 2u);
  PSX_CHECK(psx_sector_count_for_bytes(4097u) == 3u);

  // The consequence the reference lives with: for a 2049-byte file it counts 2
  // sectors and, in raw mode, transfers 2 x 2352 bytes. Reproduced here as a
  // documented shape, not as a target to hit.
  PSX_CHECK(psx_sector_count_for_bytes(2049u) * PSX_CDROM_SECTOR_SIZE == 4704u);
}

PSX_TEST(cdrom, the_cplusplus_wrapper_agrees_with_the_c_api)
{
  const Bytes sector = MakeSector(0x24u, 0x01u);
  const auto span = Whole(sector);

  const auto viaCpp = psx::cdrom::ParseHeader(span);
  psx_sector_header_t viaC{};
  const psx_error_t code = psx_sector_header_parse({sector.data(), sector.size()}, &viaC);
  PSX_CHECK(viaCpp.has_value());
  PSX_CHECK(code == PSX_OK);
  PSX_CHECK(viaCpp->submode == viaC.submode);

  const auto kindCpp = psx::cdrom::Classify(*viaCpp);
  psx_sector_kind_t kindC{};
  PSX_CHECK(kindCpp.has_value());
  PSX_CHECK(psx_sector_classify(viaC, &kindC) == PSX_OK);
  PSX_CHECK(kindCpp.value() == kindC);
  PSX_CHECK(kindCpp.value() == PSX_SECTOR_KIND_AUDIO);

  const auto payloadCpp = psx::cdrom::Payload(span);
  psx_bytes_t payloadC{};
  PSX_CHECK(psx_sector_payload({sector.data(), sector.size()}, &payloadC) == PSX_OK);
  PSX_CHECK(payloadCpp.has_value());
  PSX_CHECK(payloadCpp->size() == payloadC.size);
  PSX_CHECK(payloadCpp->size() == PSX_CDROM_FORM2_DATA_SIZE);

  const auto extent = psx::cdrom::RunExtent(span, 0u, 1u);
  PSX_CHECK(extent.has_value());
  PSX_CHECK(extent->Offset == 0u);
  // One sector in a one-sector image is the whole image: the wrapper cannot
  // report a run that runs past what it was given.
  PSX_CHECK(extent->Length == PSX_CDROM_SECTOR_SIZE);
  PSX_CHECK(!psx::cdrom::RunExtent(span, 0u, 2u).has_value());

  // Constants mirror the C defines so C++ callers do not retype 2352.
  PSX_CHECK(psx::cdrom::SectorSize == 2352u);
  PSX_CHECK(psx::cdrom::HeaderSize == 24u);
  PSX_CHECK(psx::cdrom::Form1DataSize == 2048u);
  PSX_CHECK(psx::cdrom::Form2DataSize == 2328u);
  PSX_CHECK(psx::cdrom::SectorCount(4097u) == 3u);
}

PSX_TEST(cdrom, the_new_error_code_has_a_name_and_a_group)
{
  // A code with no name prints "Unknown" in logs and test output, which is how
  // a new decoder's failure becomes invisible. Both tables must cover it.
  PSX_CHECK(std::string_view{psx_error_name(PSX_ERROR_CDROM_BAD_SECTOR_TYPE)} == "CdromBadSectorType");
  PSX_CHECK(std::string_view{psx_error_message(PSX_ERROR_CDROM_BAD_SECTOR_TYPE)}.size() > 10u);
  PSX_CHECK(psx_error_is_cdrom(PSX_ERROR_CDROM_BAD_SECTOR_TYPE) != 0);
  PSX_CHECK(psx_error_is_cdrom(PSX_ERROR_TRUNCATED) == 0);
  PSX_CHECK(psx_error_is_cdrom(PSX_ERROR_XA_UNSUPPORTED_CODING) == 0);
  PSX_CHECK(psx::ErrorMessage(psx::Error::CdromBadSectorType).size() > 10u);
  PSX_CHECK(static_cast<int>(psx::Error::CdromBadSectorType) == 200);
}
