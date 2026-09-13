// The federation entry format is a wire contract between separate
// machines, so the layout assertions in this file are the specification
// and not an implementation detail.  Changing an offset, a field width,
// the magic, or the reserved-field rule breaks every peer that already
// speaks the format, and the assertions here are what says so.
//
// The rest of the file is the receiver's side of that contract: a
// decoder that reads bytes from another machine has to reject every
// malformed shape rather than interpret it, so most tests feed it a
// deliberately broken header and demand a specific error.

#include <crucible/cipher/FederationProtocol.h>
#include <crucible/Serialize.h>  // CDAG_MAGIC
#include <crucible/Types.h>
#include <crucible/effects/OsUniverse.h>

#include "test_assert.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <type_traits>
#include <vector>

using namespace crucible;
namespace fed = crucible::cipher::federation;

// Variadic because a template argument list contains commas, which the
// preprocessor would read as separating macro arguments.
#define ASSERT_TRUE(...) assert((__VA_ARGS__))

static void test_header_layout_invariants() {
    static_assert(sizeof(fed::FederationEntryHeader) == 32);
    static_assert(alignof(fed::FederationEntryHeader) == 8);
    static_assert(std::is_standard_layout_v<fed::FederationEntryHeader>);
    static_assert(std::is_trivially_copyable_v<fed::FederationEntryHeader>);

    static_assert(offsetof(fed::FederationEntryHeader, magic) == 0);
    static_assert(offsetof(fed::FederationEntryHeader, protocol_version) == 4);
    static_assert(offsetof(fed::FederationEntryHeader, universe_cardinality) == 6);
    static_assert(offsetof(fed::FederationEntryHeader, content_hash) == 8);
    static_assert(offsetof(fed::FederationEntryHeader, row_hash) == 16);
    static_assert(offsetof(fed::FederationEntryHeader, payload_size) == 24);
    static_assert(offsetof(fed::FederationEntryHeader, reserved) == 28);

    static_assert(fed::FEDERATION_HEADER_BYTES == 32);

    std::printf("  test_header_layout_invariants:                  PASSED\n");
}

// The codec itself writes two regions, the header and the payload.  The
// 256-region case below is a compile-time witness for the cold-tier
// scale the predicate is meant to serve, and costs nothing at runtime.

static void test_cold_blob_layout_predicate() {
    static_assert(fed::federation_entry_blob_layout_disjoint(0));
    static_assert(fed::federation_entry_blob_layout_disjoint(1024));

    constexpr std::array<fed::ColdBlobRegion, 4> ok_regions{{
        {.offset_bytes = 0, .nbytes = 32},
        {.offset_bytes = 32, .nbytes = 64},
        {.offset_bytes = 128, .nbytes = 16},
        {.offset_bytes = 256, .nbytes = 0},
    }};
    static_assert(fed::cold_blob_regions_pairwise_disjoint<4>(std::span<const fed::ColdBlobRegion>{ok_regions}));

    constexpr std::array<fed::ColdBlobRegion, 3> overlap{{
        {.offset_bytes = 0, .nbytes = 64},
        {.offset_bytes = 32, .nbytes = 32},
        {.offset_bytes = 96, .nbytes = 16},
    }};
    static_assert(!fed::cold_blob_regions_pairwise_disjoint<3>(std::span<const fed::ColdBlobRegion>{overlap}));

    constexpr auto build_256_region_layout = [] {
        std::array<fed::ColdBlobRegion, 256> regions{};
        for (std::size_t i = 0; i < regions.size(); ++i) {
            regions[i] = {
                .offset_bytes = i * std::size_t{64},
                .nbytes = 64,
            };
        }
        return regions;
    };
    constexpr auto regions_256 = build_256_region_layout();
    static_assert(fed::cold_blob_regions_pairwise_disjoint<256>(std::span<const fed::ColdBlobRegion>{regions_256}));

    std::printf("  test_cold_blob_layout_predicate:                PASSED\n");
}

// The bytes C, F, E, D appear in increasing address order on the wire.
// On a little-endian load that means the constant is spelled 0x44454643,
// which reads backwards and is worth stating once.

static void test_magic_byte_order() {
    static_assert(fed::FEDERATION_MAGIC == 0x44454643u);
    static_assert((fed::FEDERATION_MAGIC & 0xFFu) == 'C');
    static_assert(((fed::FEDERATION_MAGIC >> 8) & 0xFFu) == 'F');
    static_assert(((fed::FEDERATION_MAGIC >> 16) & 0xFFu) == 'E');
    static_assert(((fed::FEDERATION_MAGIC >> 24) & 0xFFu) == 'D');

    // The same claim again through memory rather than through shifts,
    // which is how a peer decoder actually sees it.
    std::array<std::uint8_t, 4> bytes{};
    const auto magic_value = fed::FEDERATION_MAGIC;
    std::memcpy(bytes.data(), &magic_value, sizeof(magic_value));
    assert(bytes[0] == 'C');
    assert(bytes[1] == 'F');
    assert(bytes[2] == 'E');
    assert(bytes[3] == 'D');

    std::printf("  test_magic_byte_order:                          PASSED\n");
}

static void test_round_trip_basic() {
    const KernelCacheKey key{
        ContentHash{0xC0FFEE'BA'12345678ULL},
        RowHash{0xDEAD'BEEF'5678'9ABCULL},
    };
    const std::array<std::uint8_t, 8> payload = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

    std::array<std::uint8_t, 64> buf{};
    auto written = fed::serialize_federation_entry(buf, key, payload);
    ASSERT_TRUE(written.has_value());
    assert(*written == fed::FEDERATION_HEADER_BYTES + payload.size());

    const auto receiver_card = static_cast<std::uint16_t>(crucible::effects::OsUniverse::cardinality);
    auto view =
        fed::deserialize_untrusted_federation_entry(std::span<const std::uint8_t>(buf.data(), *written), receiver_card);
    ASSERT_TRUE(view.has_value());

    assert(view->header.magic == fed::FEDERATION_MAGIC);
    assert(view->header.protocol_version == fed::FEDERATION_PROTOCOL_V1);
    assert(view->header.universe_cardinality == receiver_card);
    assert(view->header.content_hash == key.content_hash);
    assert(view->header.row_hash == key.row_hash);
    assert(view->header.payload_size == payload.size());
    assert(view->header.reserved == 0u);

    assert(view->payload.size() == payload.size());
    for (std::size_t i = 0; i < payload.size(); ++i) {
        assert(view->payload[i] == payload[i]);
    }

    std::printf("  test_round_trip_basic:                          PASSED\n");
}

// An entry with no payload is legal, and means something: it announces
// possession of an artifact without shipping it, leaving the receiver to
// ask for the bytes by content hash if it wants them.

static void test_round_trip_empty_payload() {
    const KernelCacheKey key{
        ContentHash{0x1111'2222'3333'4444ULL},
        RowHash{0xAAAA'BBBB'CCCC'DDDDULL},
    };

    std::array<std::uint8_t, 32> buf{};
    auto written = fed::serialize_federation_entry(buf, key, std::span<const std::uint8_t>{});
    ASSERT_TRUE(written.has_value());
    assert(*written == fed::FEDERATION_HEADER_BYTES);  // header only

    auto view = fed::deserialize_untrusted_federation_entry(
        std::span<const std::uint8_t>(buf.data(), *written),
        static_cast<std::uint16_t>(crucible::effects::OsUniverse::cardinality));
    ASSERT_TRUE(view.has_value());
    assert(view->payload.empty());
    assert(view->header.payload_size == 0u);

    std::printf("  test_round_trip_empty_payload:                  PASSED\n");
}

// The sentinel key, both axes all ones, is the empty-slot marker of the
// open-addressing cache table.  Sending it would write an empty marker
// into a live slot on the receiver.

static void test_serialize_rejects_sentinel() {
    const auto sentinel = KernelCacheKey::sentinel();
    std::array<std::uint8_t, 64> buf{};
    auto written = fed::serialize_federation_entry(buf, sentinel, std::span<const std::uint8_t>{});
    ASSERT_TRUE(!written.has_value());
    assert(written.error() == fed::FederationError::SentinelKey);

    std::printf("  test_serialize_rejects_sentinel:                PASSED\n");
}

// A key with both axes zero is what a default-constructed key looks
// like, so a sender emitting one has forgotten to fill it in.

static void test_serialize_rejects_zero() {
    KernelCacheKey zero{};
    assert(zero.is_zero());
    assert(!zero.is_sentinel());

    std::array<std::uint8_t, 64> buf{};
    auto written = fed::serialize_federation_entry(buf, zero, std::span<const std::uint8_t>{});
    ASSERT_TRUE(!written.has_value());
    assert(written.error() == fed::FederationError::ZeroKey);

    std::printf("  test_serialize_rejects_zero:                    PASSED\n");
}

// Serialize refuses rather than truncating, because a partial entry on
// the wire desynchronizes everything after it in the stream.

static void test_serialize_rejects_undersized_buffer() {
    const KernelCacheKey key{ContentHash{0x42}, RowHash{0x43}};
    const std::array<std::uint8_t, 16> payload{};

    std::array<std::uint8_t, 40> buf{};  // 32 + 8 < 32 + 16
    auto written = fed::serialize_federation_entry(buf, key, payload);
    ASSERT_TRUE(!written.has_value());
    assert(written.error() == fed::FederationError::OutputBufferTooSmall);

    // Smaller than the header alone.
    std::array<std::uint8_t, 16> tiny_buf{};
    auto tiny_written = fed::serialize_federation_entry(tiny_buf, key, std::span<const std::uint8_t>{});
    ASSERT_TRUE(!tiny_written.has_value());
    assert(tiny_written.error() == fed::FederationError::OutputBufferTooSmall);

    std::printf("  test_serialize_rejects_undersized_buffer:       PASSED\n");
}

static void test_deserialize_rejects_truncated_header() {
    std::array<std::uint8_t, 16> tiny_buf{};
    auto view = fed::deserialize_untrusted_federation_entry(tiny_buf, 6);
    ASSERT_TRUE(!view.has_value());
    assert(view.error() == fed::FederationError::TruncatedHeader);

    auto empty_view = fed::deserialize_untrusted_federation_entry(std::span<const std::uint8_t>{}, 6);
    ASSERT_TRUE(!empty_view.has_value());
    assert(empty_view.error() == fed::FederationError::TruncatedHeader);

    std::printf("  test_deserialize_rejects_truncated_header:      PASSED\n");
}

// Everything in this header is valid except the magic, so the decoder
// has to reject on the magic alone rather than on a downstream field.

static void test_deserialize_rejects_bad_magic() {
    std::array<std::uint8_t, 32> buf{};
    fed::FederationEntryHeader hdr{};
    hdr.magic = 0xDEAD'BEEFu;  // not FEDERATION_MAGIC
    hdr.protocol_version = fed::FEDERATION_PROTOCOL_V1;
    hdr.universe_cardinality = 6u;
    hdr.content_hash = ContentHash{0x42};
    hdr.row_hash = RowHash{0x43};
    hdr.payload_size = 0u;
    hdr.reserved = 0u;
    std::memcpy(buf.data(), &hdr, sizeof(hdr));

    auto view = fed::deserialize_untrusted_federation_entry(buf, 6);
    ASSERT_TRUE(!view.has_value());
    assert(view.error() == fed::FederationError::BadMagic);

    std::printf("  test_deserialize_rejects_bad_magic:             PASSED\n");
}

// The version check is strict equality, not a minimum.  A later version
// may move a field, and reading it under this layout would misinterpret
// the bytes rather than fail.

static void test_deserialize_rejects_unsupported_version() {
    std::array<std::uint8_t, 32> buf{};
    fed::FederationEntryHeader hdr{};
    hdr.magic = fed::FEDERATION_MAGIC;
    hdr.protocol_version = 99u;  // not V1
    hdr.universe_cardinality = 6u;
    hdr.content_hash = ContentHash{0x42};
    hdr.row_hash = RowHash{0x43};
    hdr.payload_size = 0u;
    hdr.reserved = 0u;
    std::memcpy(buf.data(), &hdr, sizeof(hdr));

    auto view = fed::deserialize_untrusted_federation_entry(buf, 6);
    ASSERT_TRUE(!view.has_value());
    assert(view.error() == fed::FederationError::UnsupportedVersion);

    std::printf("  test_deserialize_rejects_unsupported_version:   PASSED\n");
}

// The reserved field is the extension point.  Rejecting a non-zero
// value is what lets a later version put a field there and still be
// sure that no older receiver quietly ignored it.

static void test_deserialize_rejects_reserved_nonzero() {
    std::array<std::uint8_t, 32> buf{};
    fed::FederationEntryHeader hdr{};
    hdr.magic = fed::FEDERATION_MAGIC;
    hdr.protocol_version = fed::FEDERATION_PROTOCOL_V1;
    hdr.universe_cardinality = 6u;
    hdr.content_hash = ContentHash{0x42};
    hdr.row_hash = RowHash{0x43};
    hdr.payload_size = 0u;
    hdr.reserved = 0xDEAD'BEEFu;
    std::memcpy(buf.data(), &hdr, sizeof(hdr));

    auto view = fed::deserialize_untrusted_federation_entry(buf, 6);
    ASSERT_TRUE(!view.has_value());
    assert(view.error() == fed::FederationError::ReservedNonZero);

    std::printf("  test_deserialize_rejects_reserved_nonzero:      PASSED\n");
}

// Each entry carries the number of effect atoms its sender knew about.
// The rule is asymmetric: a sender that knew fewer atoms than the
// receiver is fine, because every atom it used is still known and still
// means the same thing.  A sender that knew more is refused, because it
// may have keyed the entry on an atom this receiver would silently read
// as a different one.

static void test_universe_cardinality_acceptance() {
    const KernelCacheKey key{ContentHash{0x42}, RowHash{0x43}};
    auto encode_with_stamp = [&](std::uint16_t stamp, std::array<std::uint8_t, 32>& buf) {
        fed::FederationEntryHeader hdr{};
        hdr.magic = fed::FEDERATION_MAGIC;
        hdr.protocol_version = fed::FEDERATION_PROTOCOL_V1;
        hdr.universe_cardinality = stamp;
        hdr.content_hash = key.content_hash;
        hdr.row_hash = key.row_hash;
        hdr.payload_size = 0u;
        hdr.reserved = 0u;
        std::memcpy(buf.data(), &hdr, sizeof(hdr));
    };

    // Equal.
    {
        std::array<std::uint8_t, 32> buf{};
        encode_with_stamp(6, buf);
        auto view = fed::deserialize_untrusted_federation_entry(buf, 6);
        ASSERT_TRUE(view.has_value());
    }

    // Sender knew fewer.
    {
        std::array<std::uint8_t, 32> buf{};
        encode_with_stamp(3, buf);
        auto view = fed::deserialize_untrusted_federation_entry(buf, 6);
        ASSERT_TRUE(view.has_value());
    }

    // Sender knew more.
    {
        std::array<std::uint8_t, 32> buf{};
        encode_with_stamp(7, buf);
        auto view = fed::deserialize_untrusted_federation_entry(buf, 6);
        ASSERT_TRUE(!view.has_value());
        assert(view.error() == fed::FederationError::UniverseCardinalityTooHigh);
    }

    // A sender that used no atoms at all is acceptable to anyone.
    {
        std::array<std::uint8_t, 32> buf{};
        encode_with_stamp(0, buf);
        auto view = fed::deserialize_untrusted_federation_entry(buf, 6);
        ASSERT_TRUE(view.has_value());
    }

    std::printf("  test_universe_cardinality_acceptance:           PASSED\n");
}

// The same rule again as a pure predicate, which a caller can consult
// before building an entry rather than after decoding one.

static void test_accepts_cardinality_predicate() {
    static_assert(fed::federation_accepts_cardinality(0, 0));
    static_assert(fed::federation_accepts_cardinality(0, 6));
    static_assert(fed::federation_accepts_cardinality(3, 6));
    static_assert(fed::federation_accepts_cardinality(6, 6));
    static_assert(!fed::federation_accepts_cardinality(7, 6));
    static_assert(!fed::federation_accepts_cardinality(64, 6));
    static_assert(fed::federation_accepts_cardinality(64, 64));
    static_assert(!fed::federation_accepts_cardinality(65, 64));

    std::printf("  test_accepts_cardinality_predicate:             PASSED\n");
}

// The receiver repeats the sender's key checks, because the sender is
// another machine and may not have made them.

static void test_deserialize_rejects_sentinel_and_zero() {
    auto encode_key = [](const KernelCacheKey& k, std::array<std::uint8_t, 32>& buf) {
        fed::FederationEntryHeader hdr{};
        hdr.magic = fed::FEDERATION_MAGIC;
        hdr.protocol_version = fed::FEDERATION_PROTOCOL_V1;
        hdr.universe_cardinality = 6u;
        hdr.content_hash = k.content_hash;
        hdr.row_hash = k.row_hash;
        hdr.payload_size = 0u;
        hdr.reserved = 0u;
        std::memcpy(buf.data(), &hdr, sizeof(hdr));
    };

    {
        std::array<std::uint8_t, 32> buf{};
        encode_key(KernelCacheKey::sentinel(), buf);
        auto view = fed::deserialize_untrusted_federation_entry(buf, 6);
        ASSERT_TRUE(!view.has_value());
        assert(view.error() == fed::FederationError::SentinelKey);
    }

    {
        std::array<std::uint8_t, 32> buf{};
        encode_key(KernelCacheKey{}, buf);
        auto view = fed::deserialize_untrusted_federation_entry(buf, 6);
        ASSERT_TRUE(!view.has_value());
        assert(view.error() == fed::FederationError::ZeroKey);
    }

    std::printf("  test_deserialize_rejects_sentinel_and_zero:     PASSED\n");
}

// A declared payload larger than the bytes that follow is refused, not
// clipped to what is there.

static void test_deserialize_rejects_truncated_payload() {
    std::array<std::uint8_t, 36> buf{};  // 32 header + 4 payload bytes
    fed::FederationEntryHeader hdr{};
    hdr.magic = fed::FEDERATION_MAGIC;
    hdr.protocol_version = fed::FEDERATION_PROTOCOL_V1;
    hdr.universe_cardinality = 6u;
    hdr.content_hash = ContentHash{0x42};
    hdr.row_hash = RowHash{0x43};
    hdr.payload_size = 100u;  // claims 100 bytes; only 4 remain
    hdr.reserved = 0u;
    std::memcpy(buf.data(), &hdr, sizeof(hdr));

    auto view = fed::deserialize_untrusted_federation_entry(buf, 6);
    ASSERT_TRUE(!view.has_value());
    assert(view.error() == fed::FederationError::TruncatedPayload);

    std::printf("  test_deserialize_rejects_truncated_payload:     PASSED\n");
}

// The encoder must have no hidden input.  Two calls with the same key
// and payload have to produce the same bytes, or a content-addressed
// cache built on those bytes stops being content-addressed.

static void test_serialize_is_deterministic() {
    const KernelCacheKey key{
        ContentHash{0xC0FFEE'BA'12345678ULL},
        RowHash{0xDEAD'BEEF'5678'9ABCULL},
    };
    const std::array<std::uint8_t, 4> payload = {0xAA, 0xBB, 0xCC, 0xDD};

    std::array<std::uint8_t, 64> buf_a{};
    std::array<std::uint8_t, 64> buf_b{};
    auto wa = fed::serialize_federation_entry(buf_a, key, payload);
    auto wb = fed::serialize_federation_entry(buf_b, key, payload);
    ASSERT_TRUE(wa.has_value());
    ASSERT_TRUE(wb.has_value());
    assert(*wa == *wb);
    for (std::size_t i = 0; i < *wa; ++i) {
        assert(buf_a[i] == buf_b[i]);
    }

    std::printf("  test_serialize_is_deterministic:                PASSED\n");
}

// A caller that only wants the header must not get a weaker check than
// one that wants the whole entry.  The two decoders have to agree on
// every rejection, or peeking at a header would admit a stream the full
// decode would refuse.

static void test_header_overload_agreement() {
    {
        std::array<std::uint8_t, 32> buf{};
        fed::FederationEntryHeader hdr{};
        hdr.magic = 0xDEAD'BEEFu;
        hdr.protocol_version = fed::FEDERATION_PROTOCOL_V1;
        hdr.universe_cardinality = 6u;
        hdr.content_hash = ContentHash{0x42};
        hdr.row_hash = RowHash{0x43};
        hdr.payload_size = 0u;
        hdr.reserved = 0u;
        std::memcpy(buf.data(), &hdr, sizeof(hdr));

        auto h = fed::deserialize_federation_header(buf, 6);
        auto e = fed::deserialize_untrusted_federation_entry(buf, 6);
        ASSERT_TRUE(!h.has_value());
        ASSERT_TRUE(!e.has_value());
        assert(h.error() == e.error());
        assert(h.error() == fed::FederationError::BadMagic);
    }

    {
        const KernelCacheKey key{ContentHash{0x42}, RowHash{0x43}};
        std::array<std::uint8_t, 32> buf{};
        auto written = fed::serialize_federation_entry(buf, key, std::span<const std::uint8_t>{});
        ASSERT_TRUE(written.has_value());

        auto h = fed::deserialize_federation_header(buf, 6);
        auto e = fed::deserialize_untrusted_federation_entry(buf, 6);
        ASSERT_TRUE(h.has_value());
        ASSERT_TRUE(e.has_value());
        assert(h->content_hash == e->header.content_hash);
        assert(h->row_hash == e->header.row_hash);
    }

    std::printf("  test_header_overload_agreement:                 PASSED\n");
}

// A new error added to the enum without a name lands on the unknown
// fallback, which this catches.

static void test_error_name_coverage() {
    using E = fed::FederationError;
    constexpr E all[] = {
        E::None,    E::BadMagic,        E::UnsupportedVersion, E::UniverseCardinalityTooHigh, E::SentinelKey,
        E::ZeroKey, E::ReservedNonZero, E::TruncatedHeader,    E::TruncatedPayload,           E::OutputBufferTooSmall,
    };
    for (auto e : all) {
        const auto nm = fed::federation_error_name(e);
        assert(!nm.empty());
        assert(nm != "<unknown FederationError>");
    }

    assert(fed::federation_error_name(E::None) == "None");
    assert(fed::federation_error_name(E::BadMagic) == "BadMagic");
    assert(fed::federation_error_name(E::UniverseCardinalityTooHigh) == "UniverseCardinalityTooHigh");

    std::printf("  test_error_name_coverage:                       PASSED\n");
}

// The payload is every byte value once, so a codec that mangled one
// particular value has nowhere to hide.

static void test_round_trip_full_byte_range() {
    const KernelCacheKey key{
        ContentHash{0x1234'5678'9ABC'DEF0ULL},
        RowHash{0xFEDC'BA98'7654'3210ULL},
    };
    std::array<std::uint8_t, 256> payload{};
    for (std::size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<std::uint8_t>(i);
    }

    std::vector<std::uint8_t> buf(fed::FEDERATION_HEADER_BYTES + payload.size());
    auto written = fed::serialize_federation_entry(buf, key, payload);
    ASSERT_TRUE(written.has_value());

    auto view = fed::deserialize_untrusted_federation_entry(
        std::span<const std::uint8_t>(buf.data(), *written),
        static_cast<std::uint16_t>(crucible::effects::OsUniverse::cardinality));
    ASSERT_TRUE(view.has_value());
    assert(view->payload.size() == payload.size());
    for (std::size_t i = 0; i < payload.size(); ++i) {
        assert(view->payload[i] == payload[i]);
    }

    std::printf("  test_round_trip_full_byte_range:                PASSED\n");
}

// The decoded payload points into the caller's buffer rather than into
// a copy, which is what lets a receiver take a very large payload
// without allocating.  It also means the buffer has to outlive the view.

static void test_view_payload_aliases_input() {
    const KernelCacheKey key{ContentHash{0x42}, RowHash{0x43}};
    std::array<std::uint8_t, 8> payload = {1, 2, 3, 4, 5, 6, 7, 8};
    std::array<std::uint8_t, 64> buf{};
    auto written = fed::serialize_federation_entry(buf, key, payload);
    ASSERT_TRUE(written.has_value());

    auto view = fed::deserialize_untrusted_federation_entry(
        std::span<const std::uint8_t>(buf.data(), *written),
        static_cast<std::uint16_t>(crucible::effects::OsUniverse::cardinality));
    ASSERT_TRUE(view.has_value());

    assert(view->payload.data() == buf.data() + fed::FEDERATION_HEADER_BYTES);

    std::printf("  test_view_payload_aliases_input:                PASSED\n");
}

// The two key axes are distinct types in the source, but on the wire
// they are two adjacent 64-bit fields.  Swapping the values between
// them has to change the bytes, or the format would not record which
// axis a hash came from.

static void test_axis_swap_distinct_on_wire() {
    const KernelCacheKey k_normal{
        ContentHash{0x1111'1111'1111'1111ULL},
        RowHash{0x2222'2222'2222'2222ULL},
    };
    const KernelCacheKey k_swapped{
        ContentHash{0x2222'2222'2222'2222ULL},  // same values, other axis
        RowHash{0x1111'1111'1111'1111ULL},
    };

    std::array<std::uint8_t, 32> buf_n{};
    std::array<std::uint8_t, 32> buf_s{};
    auto wn = fed::serialize_federation_entry(buf_n, k_normal, std::span<const std::uint8_t>{});
    auto ws = fed::serialize_federation_entry(buf_s, k_swapped, std::span<const std::uint8_t>{});
    ASSERT_TRUE(wn.has_value());
    ASSERT_TRUE(ws.has_value());
    assert(*wn == *ws);  // same byte count
    assert(std::memcmp(buf_n.data(), buf_s.data(), *wn) != 0);  // different bytes

    std::printf("  test_axis_swap_distinct_on_wire:                PASSED\n");
}

// The receiver's cardinality is a parameter, not a compile-time
// constant read from this process.  That is what lets a caller decode
// on behalf of a peer, and what this test pins by driving one buffer
// against three different receiver values.

static void test_receiver_cardinality_is_explicit() {
    const KernelCacheKey key{ContentHash{0x42}, RowHash{0x43}};
    std::array<std::uint8_t, 32> buf{};

    fed::FederationEntryHeader hdr{};
    hdr.magic = fed::FEDERATION_MAGIC;
    hdr.protocol_version = fed::FEDERATION_PROTOCOL_V1;
    hdr.universe_cardinality = 10u;
    hdr.content_hash = key.content_hash;
    hdr.row_hash = key.row_hash;
    hdr.payload_size = 0u;
    hdr.reserved = 0u;
    std::memcpy(buf.data(), &hdr, sizeof(hdr));

    auto rej = fed::deserialize_untrusted_federation_entry(buf, 5);
    ASSERT_TRUE(!rej.has_value());
    assert(rej.error() == fed::FederationError::UniverseCardinalityTooHigh);

    auto eq = fed::deserialize_untrusted_federation_entry(buf, 10);
    ASSERT_TRUE(eq.has_value());

    auto big = fed::deserialize_untrusted_federation_entry(buf, 100);
    ASSERT_TRUE(big.has_value());

    std::printf("  test_receiver_cardinality_is_explicit:          PASSED\n");
}

// The returned error is the only failure channel the codec has, so
// every entry point stays noexcept.

static void test_codec_is_noexcept() {
    std::array<std::uint8_t, 64> buf{};
    const KernelCacheKey key{ContentHash{0x42}, RowHash{0x43}};
    static_assert(noexcept(fed::serialize_federation_entry(buf, key, std::span<const std::uint8_t>{})));
    static_assert(noexcept(fed::deserialize_federation_header(std::span<const std::uint8_t>{}, std::uint16_t{6})));
    static_assert(
        noexcept(fed::deserialize_untrusted_federation_entry(std::span<const std::uint8_t>{}, std::uint16_t{6})));
    static_assert(noexcept(fed::federation_accepts_cardinality(std::uint16_t{6}, std::uint16_t{6})));
    static_assert(noexcept(fed::federation_error_name(fed::FederationError::None)));

    std::printf("  test_codec_is_noexcept:                         PASSED\n");
}

// The cardinality field is 16 bits, so the largest value it can hold is
// the interesting case for the comparison it feeds.

static void test_audit_a_cardinality_boundary_uint16_max() {
    constexpr std::uint16_t MAX_CARD = 0xFFFFu;

    static_assert(fed::federation_accepts_cardinality(MAX_CARD, MAX_CARD));
    static_assert(fed::federation_accepts_cardinality(0, MAX_CARD));
    static_assert(!fed::federation_accepts_cardinality(MAX_CARD, MAX_CARD - 1));

    std::array<std::uint8_t, 32> buf{};
    fed::FederationEntryHeader hdr{};
    hdr.magic = fed::FEDERATION_MAGIC;
    hdr.protocol_version = fed::FEDERATION_PROTOCOL_V1;
    hdr.universe_cardinality = MAX_CARD;
    hdr.content_hash = ContentHash{0x42};
    hdr.row_hash = RowHash{0x43};
    hdr.payload_size = 0u;
    hdr.reserved = 0u;
    std::memcpy(buf.data(), &hdr, sizeof(hdr));

    auto accept = fed::deserialize_untrusted_federation_entry(buf, MAX_CARD);
    ASSERT_TRUE(accept.has_value());
    assert(accept->header.universe_cardinality == MAX_CARD);

    auto reject_one_below = fed::deserialize_untrusted_federation_entry(buf, MAX_CARD - 1u);
    ASSERT_TRUE(!reject_one_below.has_value());
    assert(reject_one_below.error() == fed::FederationError::UniverseCardinalityTooHigh);

    std::printf("  [AUDIT-A] cardinality_boundary_uint16_max:      PASSED\n");
}

// An exactly-sized buffer must be accepted.  An off-by-one in the size
// check would refuse it, and the refusal reads as a legitimate
// too-small error rather than as a bug.

static void test_audit_b_buffer_exactly_fits() {
    const KernelCacheKey key{
        ContentHash{0x1111'2222'3333'4444ULL},
        RowHash{0x5555'6666'7777'8888ULL},
    };
    const std::array<std::uint8_t, 16> payload = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

    std::array<std::uint8_t, 48> buf{};  // 32 + 16, exactly
    auto written = fed::serialize_federation_entry(buf, key, payload);
    ASSERT_TRUE(written.has_value());
    assert(*written == 48u);

    std::array<std::uint8_t, 47> short_buf{};
    auto rejected = fed::serialize_federation_entry(short_buf, key, payload);
    ASSERT_TRUE(!rejected.has_value());
    assert(rejected.error() == fed::FederationError::OutputBufferTooSmall);

    // The same boundary with no payload at all.
    std::array<std::uint8_t, 32> tight_buf{};
    auto tight = fed::serialize_federation_entry(tight_buf, key, std::span<const std::uint8_t>{});
    ASSERT_TRUE(tight.has_value());
    assert(*tight == 32u);

    std::printf("  [AUDIT-B] buffer_exactly_fits:                   PASSED\n");
}

// A transport that batches several entries into one buffer hands the
// decoder more bytes than the first entry occupies.  The payload span
// has to stop at the declared size, or the first entry would swallow
// the second.

static void test_audit_c_extra_bytes_at_end() {
    const KernelCacheKey key{ContentHash{0x42}, RowHash{0x43}};
    const std::array<std::uint8_t, 4> payload = {0xAA, 0xBB, 0xCC, 0xDD};

    std::array<std::uint8_t, 64> buf{};
    auto written = fed::serialize_federation_entry(buf, key, payload);
    ASSERT_TRUE(written.has_value());
    assert(*written == 36u);

    // A value that appears nowhere in the payload, so its absence from
    // the decoded span is meaningful.
    for (std::size_t i = 36; i < buf.size(); ++i) {
        buf[i] = 0xEE;
    }

    // Decode over the whole buffer, not over the bytes just written.
    auto view = fed::deserialize_untrusted_federation_entry(
        buf, static_cast<std::uint16_t>(crucible::effects::OsUniverse::cardinality));
    ASSERT_TRUE(view.has_value());
    assert(view->header.payload_size == 4u);
    assert(view->payload.size() == 4u);
    assert(view->payload[0] == 0xAA);
    assert(view->payload[1] == 0xBB);
    assert(view->payload[2] == 0xCC);
    assert(view->payload[3] == 0xDD);

    std::printf("  [AUDIT-C] extra_bytes_at_end:                    PASSED\n");
}

// Only the full sentinel is refused.  A key with one axis all ones and
// the other a real hash is a legal key: improbable under any decent
// hash, but nothing forbids it, and refusing it would drop real
// entries.  The same holds for a single zero axis.

static void test_audit_d_partial_sentinel_accepted() {
    constexpr std::uint64_t MAX = std::numeric_limits<std::uint64_t>::max();

    {
        const KernelCacheKey key{
            ContentHash{MAX},
            RowHash{0xAAAA'BBBB'CCCC'DDDDULL},
        };
        assert(!key.is_sentinel());
        assert(!key.is_zero());

        std::array<std::uint8_t, 32> buf{};
        auto written = fed::serialize_federation_entry(buf, key, std::span<const std::uint8_t>{});
        ASSERT_TRUE(written.has_value());

        auto view = fed::deserialize_untrusted_federation_entry(
            buf, static_cast<std::uint16_t>(crucible::effects::OsUniverse::cardinality));
        ASSERT_TRUE(view.has_value());
        assert(view->header.content_hash.raw() == MAX);
    }

    {
        const KernelCacheKey key{
            ContentHash{0x1234'5678'9ABC'DEF0ULL},
            RowHash{MAX},
        };
        assert(!key.is_sentinel());
        assert(!key.is_zero());

        std::array<std::uint8_t, 32> buf{};
        auto written = fed::serialize_federation_entry(buf, key, std::span<const std::uint8_t>{});
        ASSERT_TRUE(written.has_value());

        auto view = fed::deserialize_untrusted_federation_entry(
            buf, static_cast<std::uint16_t>(crucible::effects::OsUniverse::cardinality));
        ASSERT_TRUE(view.has_value());
        assert(view->header.row_hash.raw() == MAX);
    }

    {
        const KernelCacheKey key{
            ContentHash{0u},
            RowHash{0xDEAD'BEEFULL},
        };
        assert(!key.is_zero());
        assert(!key.is_sentinel());

        std::array<std::uint8_t, 32> buf{};
        auto written = fed::serialize_federation_entry(buf, key, std::span<const std::uint8_t>{});
        ASSERT_TRUE(written.has_value());
    }

    std::printf("  [AUDIT-D] partial_sentinel_accepted:             PASSED\n");
}

// Nothing forbids the two axes from holding the same 64 bits, and the
// codec must not treat that as an error or collapse the two fields.

static void test_audit_e_same_bit_pattern_axes() {
    constexpr std::uint64_t SHARED_BITS = 0xDEAD'BEEF'CAFE'BABEULL;
    const KernelCacheKey key{
        ContentHash{SHARED_BITS},
        RowHash{SHARED_BITS},
    };
    const std::array<std::uint8_t, 4> payload = {0x01, 0x02, 0x03, 0x04};

    std::array<std::uint8_t, 64> buf{};
    auto written = fed::serialize_federation_entry(buf, key, payload);
    ASSERT_TRUE(written.has_value());

    auto view = fed::deserialize_untrusted_federation_entry(
        std::span<const std::uint8_t>(buf.data(), *written),
        static_cast<std::uint16_t>(crucible::effects::OsUniverse::cardinality));
    ASSERT_TRUE(view.has_value());
    assert(view->header.content_hash.raw() == SHARED_BITS);
    assert(view->header.row_hash.raw() == SHARED_BITS);
    assert(view->header.content_hash == key.content_hash);
    assert(view->header.row_hash == key.row_hash);

    // With both axes equal the two hash fields must be byte-identical
    // in the buffer, which pins where each one sits.
    for (std::size_t i = 0; i < 8; ++i) {
        assert(buf[8 + i] == buf[16 + i]);
    }

    std::printf("  [AUDIT-E] same_bit_pattern_axes:                 PASSED\n");
}

// The query side folds at compile time.  The codec itself does not, and
// is not meant to: it copies bytes through a buffer, which is a runtime
// operation.

namespace audit_f_constexpr_witnesses {
static_assert(fed::federation_accepts_cardinality(0, 0));
static_assert(fed::federation_accepts_cardinality(6, 6));
static_assert(!fed::federation_accepts_cardinality(7, 6));

static_assert(fed::federation_error_name(fed::FederationError::None) == "None");
static_assert(fed::federation_error_name(fed::FederationError::BadMagic) == "BadMagic");
static_assert(fed::federation_error_name(fed::FederationError::UniverseCardinalityTooHigh)
              == "UniverseCardinalityTooHigh");

static_assert(fed::FEDERATION_MAGIC == 0x44454643u);
static_assert(fed::FEDERATION_PROTOCOL_V1 == 1u);
static_assert(fed::FEDERATION_HEADER_BYTES == 32u);
}  // namespace audit_f_constexpr_witnesses

// The same two calls again at runtime, so that making either function
// non-constexpr breaks the assertions above without silently removing
// the check itself.
static void test_audit_f_constexpr_witnesses() {
    [[maybe_unused]] auto card_check = fed::federation_accepts_cardinality(6, 6);
    [[maybe_unused]] auto name_check = fed::federation_error_name(fed::FederationError::None);
    assert(card_check);
    assert(name_check == "None");

    std::printf("  [AUDIT-F] constexpr_witnesses:                   PASSED\n");
}

// A federation stream and a graph snapshot are told apart by their
// magic alone.  If the two constants ever became equal, each stream
// would dispatch to the other's decoder, so this file pulls in both
// headers to pin the inequality in one place.

static void test_audit_g_magic_collision_with_cdag() {
    static_assert(fed::FEDERATION_MAGIC == 0x44454643u);
    static_assert(crucible::CDAG_MAGIC == 0x43444147u);
    static_assert(fed::FEDERATION_MAGIC != crucible::CDAG_MAGIC, "FEDERATION_MAGIC must not collide with CDAG_MAGIC.");

    // The first byte on the wire already differs, so a decoder that
    // reads one byte at a time rejects the wrong stream immediately.
    constexpr auto fed_first_byte = static_cast<std::uint8_t>(fed::FEDERATION_MAGIC & 0xFFu);
    constexpr auto cdag_first_byte = static_cast<std::uint8_t>(crucible::CDAG_MAGIC & 0xFFu);
    static_assert(fed_first_byte == 'C');
    static_assert(cdag_first_byte == 'G');
    static_assert(fed_first_byte != cdag_first_byte);

    std::printf("  [AUDIT-G] magic_collision_with_cdag:             PASSED\n");
}

static void test_audit_h_field_width_pins() {
    static_assert(sizeof(fed::FederationEntryHeader::magic) == 4);
    static_assert(sizeof(fed::FederationEntryHeader::protocol_version) == 2);
    static_assert(sizeof(fed::FederationEntryHeader::universe_cardinality) == 2);
    static_assert(sizeof(fed::FederationEntryHeader::content_hash) == 8);
    static_assert(sizeof(fed::FederationEntryHeader::row_hash) == 8);
    static_assert(sizeof(fed::FederationEntryHeader::payload_size) == 4);
    static_assert(sizeof(fed::FederationEntryHeader::reserved) == 4);

    // The widths sum to the header size, so the layout carries no
    // padding and no compiler can insert any.
    static_assert(4 + 2 + 2 + 8 + 8 + 4 + 4 == 32);

    std::printf("  [AUDIT-H] field_width_pins:                      PASSED\n");
}

// The payload alternates two values, so a copy shifted by one byte
// comes back inverted rather than equal.

static void test_audit_i_vector_buffer_roundtrip() {
    const KernelCacheKey key{
        ContentHash{0x0F0F'0F0F'0F0F'0F0FULL},
        RowHash{0xF0F0'F0F0'F0F0'F0F0ULL},
    };

    std::vector<std::uint8_t> payload(1024);
    for (std::size_t i = 0; i < payload.size(); ++i) {
        payload[i] = (i & 1u) ? 0xAAu : 0x55u;
    }

    std::vector<std::uint8_t> buf(fed::FEDERATION_HEADER_BYTES + payload.size());
    auto written = fed::serialize_federation_entry(buf, key, payload);
    ASSERT_TRUE(written.has_value());
    assert(*written == buf.size());

    auto view = fed::deserialize_untrusted_federation_entry(
        std::span<const std::uint8_t>(buf.data(), *written),
        static_cast<std::uint16_t>(crucible::effects::OsUniverse::cardinality));
    ASSERT_TRUE(view.has_value());
    assert(view->payload.size() == payload.size());
    for (std::size_t i = 0; i < payload.size(); ++i) {
        assert(view->payload[i] == payload[i]);
    }

    std::printf("  [AUDIT-I] vector_buffer_roundtrip:               PASSED\n");
}

int main() {
    std::printf("test_federation_protocol — wire-format witness\n");
    test_header_layout_invariants();
    test_cold_blob_layout_predicate();
    test_magic_byte_order();
    test_round_trip_basic();
    test_round_trip_empty_payload();
    test_serialize_rejects_sentinel();
    test_serialize_rejects_zero();
    test_serialize_rejects_undersized_buffer();
    test_deserialize_rejects_truncated_header();
    test_deserialize_rejects_bad_magic();
    test_deserialize_rejects_unsupported_version();
    test_deserialize_rejects_reserved_nonzero();
    test_universe_cardinality_acceptance();
    test_accepts_cardinality_predicate();
    test_deserialize_rejects_sentinel_and_zero();
    test_deserialize_rejects_truncated_payload();
    test_serialize_is_deterministic();
    test_header_overload_agreement();
    test_error_name_coverage();
    test_round_trip_full_byte_range();
    test_view_payload_aliases_input();
    test_axis_swap_distinct_on_wire();
    test_receiver_cardinality_is_explicit();
    test_codec_is_noexcept();
    std::printf("--- audit groups ---\n");
    test_audit_a_cardinality_boundary_uint16_max();
    test_audit_b_buffer_exactly_fits();
    test_audit_c_extra_bytes_at_end();
    test_audit_d_partial_sentinel_accepted();
    test_audit_e_same_bit_pattern_axes();
    test_audit_f_constexpr_witnesses();
    test_audit_g_magic_collision_with_cdag();
    test_audit_h_field_width_pins();
    test_audit_i_vector_buffer_roundtrip();
    std::printf("test_federation_protocol: 24 + 9 audit groups, all passed\n");
    return 0;
}
