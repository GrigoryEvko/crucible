// FOUND-I08: Federation network protocol spec for row-keyed entries.
//
// Verifies the wire-format contract of cipher::federation:
//
//   • InitSafe  — every header field has NSDMI; default-constructed
//                 header is fully specified zero.
//   • TypeSafe  — std::expected<T, FederationError> error channel is
//                 typed; FederationError uses a strong enum class.
//                 ContentHash + RowHash strong IDs prevent axis swap.
//   • NullSafe  — std::span surfaces; nullptr+0 spans handled cleanly.
//   • MemSafe   — std::memcpy round-trips through trivially-copyable
//                 standard-layout struct losslessly.  No raw new/delete.
//   • DetSafe   — same (key, payload) → same 32+N bytes; round-trip
//                 is bit-identical.
//   • Wire format invariants — magic byte order, header offsets,
//                 protocol-version equality, reserved-field zero.
//   • FOUND-I04 invariant on the wire — Universe-cardinality stamping
//                 rejects newer-sender entries; older-sender entries
//                 are accepted.
//   • Sentinel + zero key rejection — both axes UINT64_MAX (sentinel)
//                 and both axes 0 (zero) are refused on serialize AND
//                 deserialize.

#include <crucible/cipher/FederationProtocol.h>
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

// Variadic ASSERT_TRUE — sidesteps the `assert(cond)` macro-arg
// comma issue for template-arg-list expressions (see also
// test_computation_cache_integration.cpp + test_computation_cache_invalidation.cpp).
#define ASSERT_TRUE(...) assert((__VA_ARGS__))

// ─────────────────────────────────────────────────────────────────────
// Group 1 — header layout invariants (compile-time pinning).

static void test_header_layout_invariants() {
    static_assert(sizeof(fed::FederationEntryHeader) == 32);
    static_assert(alignof(fed::FederationEntryHeader) == 8);
    static_assert(std::is_standard_layout_v<fed::FederationEntryHeader>);
    static_assert(std::is_trivially_copyable_v<fed::FederationEntryHeader>);

    static_assert(offsetof(fed::FederationEntryHeader, magic)                ==  0);
    static_assert(offsetof(fed::FederationEntryHeader, protocol_version)     ==  4);
    static_assert(offsetof(fed::FederationEntryHeader, universe_cardinality) ==  6);
    static_assert(offsetof(fed::FederationEntryHeader, content_hash)         ==  8);
    static_assert(offsetof(fed::FederationEntryHeader, row_hash)             == 16);
    static_assert(offsetof(fed::FederationEntryHeader, payload_size)         == 24);
    static_assert(offsetof(fed::FederationEntryHeader, reserved)             == 28);

    static_assert(fed::FEDERATION_HEADER_BYTES == 32);

    std::printf("  test_header_layout_invariants:                  PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// Group 2 — magic byte order.  The bytes 'C','F','E','D' must appear
// in increasing memory address order, which on a little-endian load
// means the uint32_t magic constant has the spelling 0x44454643.

static void test_magic_byte_order() {
    static_assert(fed::FEDERATION_MAGIC == 0x44454643u);
    static_assert((fed::FEDERATION_MAGIC      & 0xFFu) == 'C');
    static_assert(((fed::FEDERATION_MAGIC>> 8) & 0xFFu) == 'F');
    static_assert(((fed::FEDERATION_MAGIC>>16) & 0xFFu) == 'E');
    static_assert(((fed::FEDERATION_MAGIC>>24) & 0xFFu) == 'D');

    // Runtime witness: write the magic into a buffer and inspect
    // bytes by index.  This is the codec's-eye view of the magic.
    std::array<std::uint8_t, 4> bytes{};
    const auto magic_value = fed::FEDERATION_MAGIC;
    std::memcpy(bytes.data(), &magic_value, sizeof(magic_value));
    assert(bytes[0] == 'C');
    assert(bytes[1] == 'F');
    assert(bytes[2] == 'E');
    assert(bytes[3] == 'D');

    std::printf("  test_magic_byte_order:                          PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// Group 3 — round-trip a populated entry (header + payload).

static void test_round_trip_basic() {
    const KernelCacheKey key{
        ContentHash{0xC0FFEE'BA'12345678ULL},
        RowHash{0xDEAD'BEEF'5678'9ABCULL},
    };
    const std::array<std::uint8_t, 8> payload = {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08
    };

    std::array<std::uint8_t, 64> buf{};
    auto written = fed::serialize_federation_entry(buf, key, payload);
    ASSERT_TRUE(written.has_value());
    assert(*written == fed::FEDERATION_HEADER_BYTES + payload.size());

    const auto receiver_card =
        static_cast<std::uint16_t>(crucible::effects::OsUniverse::cardinality);
    auto view = fed::deserialize_federation_entry(
        std::span<const std::uint8_t>(buf.data(), *written),
        receiver_card);
    ASSERT_TRUE(view.has_value());

    // Header round-trips bit-exact.
    assert(view->header.magic == fed::FEDERATION_MAGIC);
    assert(view->header.protocol_version == fed::FEDERATION_PROTOCOL_V1);
    assert(view->header.universe_cardinality == receiver_card);
    assert(view->header.content_hash == key.content_hash);
    assert(view->header.row_hash     == key.row_hash);
    assert(view->header.payload_size == payload.size());
    assert(view->header.reserved == 0u);

    // Payload round-trips byte-identical.
    assert(view->payload.size() == payload.size());
    for (std::size_t i = 0; i < payload.size(); ++i) {
        assert(view->payload[i] == payload[i]);
    }

    std::printf("  test_round_trip_basic:                          PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// Group 4 — empty payload is accepted (announce a key without bytes).
// A zero-byte payload encodes "I have artifact X; ask me for it via
// content_hash if you want it" — the dedup-confirmation pattern.

static void test_round_trip_empty_payload() {
    const KernelCacheKey key{
        ContentHash{0x1111'2222'3333'4444ULL},
        RowHash{0xAAAA'BBBB'CCCC'DDDDULL},
    };

    std::array<std::uint8_t, 32> buf{};
    auto written = fed::serialize_federation_entry(
        buf, key, std::span<const std::uint8_t>{});
    ASSERT_TRUE(written.has_value());
    assert(*written == fed::FEDERATION_HEADER_BYTES);  // header only

    auto view = fed::deserialize_federation_entry(
        std::span<const std::uint8_t>(buf.data(), *written),
        static_cast<std::uint16_t>(crucible::effects::OsUniverse::cardinality));
    ASSERT_TRUE(view.has_value());
    assert(view->payload.empty());
    assert(view->header.payload_size == 0u);

    std::printf("  test_round_trip_empty_payload:                  PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// Group 5 — sentinel key rejection on serialize.  KernelCacheKey::
// sentinel() (both axes UINT64_MAX) is the open-addressing-table
// empty-slot marker (Types.h::KernelCacheKey).  Federating it would
// poison the receiver's cache.

static void test_serialize_rejects_sentinel() {
    const auto sentinel = KernelCacheKey::sentinel();
    std::array<std::uint8_t, 64> buf{};
    auto written = fed::serialize_federation_entry(
        buf, sentinel, std::span<const std::uint8_t>{});
    ASSERT_TRUE(!written.has_value());
    assert(written.error() == fed::FederationError::SentinelKey);

    std::printf("  test_serialize_rejects_sentinel:                PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// Group 6 — zero-key rejection on serialize.  Default-constructed
// KernelCacheKey has both axes 0; a sender that emits this is buggy
// (forgot to populate).

static void test_serialize_rejects_zero() {
    KernelCacheKey zero{};
    assert(zero.is_zero());
    assert(!zero.is_sentinel());

    std::array<std::uint8_t, 64> buf{};
    auto written = fed::serialize_federation_entry(
        buf, zero, std::span<const std::uint8_t>{});
    ASSERT_TRUE(!written.has_value());
    assert(written.error() == fed::FederationError::ZeroKey);

    std::printf("  test_serialize_rejects_zero:                    PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// Group 7 — output-buffer-too-small rejection.  Serialize must refuse
// to truncate; a partial write would corrupt federation traffic.

static void test_serialize_rejects_undersized_buffer() {
    const KernelCacheKey key{ContentHash{0x42}, RowHash{0x43}};
    const std::array<std::uint8_t, 16> payload{};

    // Buffer big enough for header but not payload.
    std::array<std::uint8_t, 40> buf{};  // 32 + 8 < 32 + 16
    auto written = fed::serialize_federation_entry(buf, key, payload);
    ASSERT_TRUE(!written.has_value());
    assert(written.error() == fed::FederationError::OutputBufferTooSmall);

    // Buffer too small for even the header.
    std::array<std::uint8_t, 16> tiny_buf{};
    auto tiny_written = fed::serialize_federation_entry(
        tiny_buf, key, std::span<const std::uint8_t>{});
    ASSERT_TRUE(!tiny_written.has_value());
    assert(tiny_written.error() == fed::FederationError::OutputBufferTooSmall);

    std::printf("  test_serialize_rejects_undersized_buffer:       PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// Group 8 — deserialize rejects truncated header.  An input buffer
// shorter than 32 bytes can't possibly be a valid entry.

static void test_deserialize_rejects_truncated_header() {
    std::array<std::uint8_t, 16> tiny_buf{};
    auto view = fed::deserialize_federation_entry(tiny_buf, 6);
    ASSERT_TRUE(!view.has_value());
    assert(view.error() == fed::FederationError::TruncatedHeader);

    // Empty span.
    auto empty_view = fed::deserialize_federation_entry(
        std::span<const std::uint8_t>{}, 6);
    ASSERT_TRUE(!empty_view.has_value());
    assert(empty_view.error() == fed::FederationError::TruncatedHeader);

    std::printf("  test_deserialize_rejects_truncated_header:      PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// Group 9 — deserialize rejects bad magic.  Wrong magic = not a
// Crucible federation stream.

static void test_deserialize_rejects_bad_magic() {
    std::array<std::uint8_t, 32> buf{};
    // Write a header with wrong magic but otherwise valid.
    fed::FederationEntryHeader hdr{};
    hdr.magic = 0xDEAD'BEEFu;  // not FEDERATION_MAGIC
    hdr.protocol_version = fed::FEDERATION_PROTOCOL_V1;
    hdr.universe_cardinality = 6u;
    hdr.content_hash = ContentHash{0x42};
    hdr.row_hash     = RowHash{0x43};
    hdr.payload_size = 0u;
    hdr.reserved     = 0u;
    std::memcpy(buf.data(), &hdr, sizeof(hdr));

    auto view = fed::deserialize_federation_entry(buf, 6);
    ASSERT_TRUE(!view.has_value());
    assert(view.error() == fed::FederationError::BadMagic);

    std::printf("  test_deserialize_rejects_bad_magic:             PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// Group 10 — deserialize rejects unsupported version.  A V2 header
// with V1 layout would silently misinterpret 4 bytes; strict equality
// is the only safe check.

static void test_deserialize_rejects_unsupported_version() {
    std::array<std::uint8_t, 32> buf{};
    fed::FederationEntryHeader hdr{};
    hdr.magic = fed::FEDERATION_MAGIC;
    hdr.protocol_version = 99u;  // not V1
    hdr.universe_cardinality = 6u;
    hdr.content_hash = ContentHash{0x42};
    hdr.row_hash     = RowHash{0x43};
    hdr.payload_size = 0u;
    hdr.reserved     = 0u;
    std::memcpy(buf.data(), &hdr, sizeof(hdr));

    auto view = fed::deserialize_federation_entry(buf, 6);
    ASSERT_TRUE(!view.has_value());
    assert(view.error() == fed::FederationError::UnsupportedVersion);

    std::printf("  test_deserialize_rejects_unsupported_version:   PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// Group 11 — deserialize rejects non-zero reserved field.  Future-
// extension gate: V2 fields will live here, V1 receivers must reject
// rather than silently accept.

static void test_deserialize_rejects_reserved_nonzero() {
    std::array<std::uint8_t, 32> buf{};
    fed::FederationEntryHeader hdr{};
    hdr.magic = fed::FEDERATION_MAGIC;
    hdr.protocol_version = fed::FEDERATION_PROTOCOL_V1;
    hdr.universe_cardinality = 6u;
    hdr.content_hash = ContentHash{0x42};
    hdr.row_hash     = RowHash{0x43};
    hdr.payload_size = 0u;
    hdr.reserved     = 0xDEAD'BEEFu;  // V2 fields would live here
    std::memcpy(buf.data(), &hdr, sizeof(hdr));

    auto view = fed::deserialize_federation_entry(buf, 6);
    ASSERT_TRUE(!view.has_value());
    assert(view.error() == fed::FederationError::ReservedNonZero);

    std::printf("  test_deserialize_rejects_reserved_nonzero:      PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// Group 12 — Universe-cardinality stamping (FOUND-I04 on the wire).
//
// Three scenarios:
//   (a) sender_card == receiver_card → ACCEPT
//   (b) sender_card  < receiver_card → ACCEPT (older sender, newer
//       receiver — every atom the sender used is still known)
//   (c) sender_card  > receiver_card → REJECT (newer sender used
//       atoms the receiver can't interpret — silent collision risk)

static void test_universe_cardinality_acceptance() {
    const KernelCacheKey key{ContentHash{0x42}, RowHash{0x43}};
    auto encode_with_stamp = [&](std::uint16_t stamp,
                                 std::array<std::uint8_t, 32>& buf) {
        fed::FederationEntryHeader hdr{};
        hdr.magic = fed::FEDERATION_MAGIC;
        hdr.protocol_version = fed::FEDERATION_PROTOCOL_V1;
        hdr.universe_cardinality = stamp;
        hdr.content_hash = key.content_hash;
        hdr.row_hash     = key.row_hash;
        hdr.payload_size = 0u;
        hdr.reserved     = 0u;
        std::memcpy(buf.data(), &hdr, sizeof(hdr));
    };

    // (a) equal cardinality → accept.
    {
        std::array<std::uint8_t, 32> buf{};
        encode_with_stamp(6, buf);
        auto view = fed::deserialize_federation_entry(buf, 6);
        ASSERT_TRUE(view.has_value());
    }

    // (b) sender < receiver → accept (older sender, atoms still known).
    {
        std::array<std::uint8_t, 32> buf{};
        encode_with_stamp(3, buf);
        auto view = fed::deserialize_federation_entry(buf, 6);
        ASSERT_TRUE(view.has_value());
    }

    // (c) sender > receiver → reject (atoms unknown to receiver).
    {
        std::array<std::uint8_t, 32> buf{};
        encode_with_stamp(7, buf);
        auto view = fed::deserialize_federation_entry(buf, 6);
        ASSERT_TRUE(!view.has_value());
        assert(view.error() ==
               fed::FederationError::UniverseCardinalityTooHigh);
    }

    // (d) edge: sender = 0 (no atoms used) → accept on any receiver.
    {
        std::array<std::uint8_t, 32> buf{};
        encode_with_stamp(0, buf);
        auto view = fed::deserialize_federation_entry(buf, 6);
        ASSERT_TRUE(view.has_value());
    }

    std::printf("  test_universe_cardinality_acceptance:           PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// Group 13 — federation_accepts_cardinality predicate witness.
// Pure consteval boolean; mirrors the runtime check.

static void test_accepts_cardinality_predicate() {
    static_assert( fed::federation_accepts_cardinality(0, 0));
    static_assert( fed::federation_accepts_cardinality(0, 6));
    static_assert( fed::federation_accepts_cardinality(3, 6));
    static_assert( fed::federation_accepts_cardinality(6, 6));
    static_assert(!fed::federation_accepts_cardinality(7, 6));
    static_assert(!fed::federation_accepts_cardinality(64, 6));
    static_assert( fed::federation_accepts_cardinality(64, 64));
    static_assert(!fed::federation_accepts_cardinality(65, 64));

    std::printf("  test_accepts_cardinality_predicate:             PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// Group 14 — deserialize rejects sentinel + zero keys.  Mirrors
// serialize-side rejection for the receiver path.

static void test_deserialize_rejects_sentinel_and_zero() {
    auto encode_key = [](const KernelCacheKey& k,
                         std::array<std::uint8_t, 32>& buf) {
        fed::FederationEntryHeader hdr{};
        hdr.magic = fed::FEDERATION_MAGIC;
        hdr.protocol_version = fed::FEDERATION_PROTOCOL_V1;
        hdr.universe_cardinality = 6u;
        hdr.content_hash = k.content_hash;
        hdr.row_hash     = k.row_hash;
        hdr.payload_size = 0u;
        hdr.reserved     = 0u;
        std::memcpy(buf.data(), &hdr, sizeof(hdr));
    };

    // Sentinel.
    {
        std::array<std::uint8_t, 32> buf{};
        encode_key(KernelCacheKey::sentinel(), buf);
        auto view = fed::deserialize_federation_entry(buf, 6);
        ASSERT_TRUE(!view.has_value());
        assert(view.error() == fed::FederationError::SentinelKey);
    }

    // Zero (default-constructed).
    {
        std::array<std::uint8_t, 32> buf{};
        encode_key(KernelCacheKey{}, buf);
        auto view = fed::deserialize_federation_entry(buf, 6);
        ASSERT_TRUE(!view.has_value());
        assert(view.error() == fed::FederationError::ZeroKey);
    }

    std::printf("  test_deserialize_rejects_sentinel_and_zero:     PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// Group 15 — deserialize rejects truncated payload.  declared
// payload_size > bytes remaining after header → reject (not silently
// truncate).

static void test_deserialize_rejects_truncated_payload() {
    std::array<std::uint8_t, 36> buf{};  // 32 header + 4 payload bytes
    fed::FederationEntryHeader hdr{};
    hdr.magic = fed::FEDERATION_MAGIC;
    hdr.protocol_version = fed::FEDERATION_PROTOCOL_V1;
    hdr.universe_cardinality = 6u;
    hdr.content_hash = ContentHash{0x42};
    hdr.row_hash     = RowHash{0x43};
    hdr.payload_size = 100u;  // claims 100 bytes; only 4 remain
    hdr.reserved     = 0u;
    std::memcpy(buf.data(), &hdr, sizeof(hdr));

    auto view = fed::deserialize_federation_entry(buf, 6);
    ASSERT_TRUE(!view.has_value());
    assert(view.error() == fed::FederationError::TruncatedPayload);

    std::printf("  test_deserialize_rejects_truncated_payload:     PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// Group 16 — bit-identical determinism witness.  Same inputs → same
// 32+N bytes, every time.  Pins DetSafe at the codec level.

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

// ─────────────────────────────────────────────────────────────────────
// Group 17 — deserialize_federation_header is a strict subset of
// deserialize_federation_entry.  Both must agree on rejection rules
// (the entry overload calls the header overload internally).

static void test_header_overload_agreement() {
    // Bad magic → both reject identically.
    {
        std::array<std::uint8_t, 32> buf{};
        fed::FederationEntryHeader hdr{};
        hdr.magic = 0xDEAD'BEEFu;
        hdr.protocol_version = fed::FEDERATION_PROTOCOL_V1;
        hdr.universe_cardinality = 6u;
        hdr.content_hash = ContentHash{0x42};
        hdr.row_hash     = RowHash{0x43};
        hdr.payload_size = 0u;
        hdr.reserved     = 0u;
        std::memcpy(buf.data(), &hdr, sizeof(hdr));

        auto h = fed::deserialize_federation_header(buf, 6);
        auto e = fed::deserialize_federation_entry(buf, 6);
        ASSERT_TRUE(!h.has_value());
        ASSERT_TRUE(!e.has_value());
        assert(h.error() == e.error());
        assert(h.error() == fed::FederationError::BadMagic);
    }

    // Good entry → both accept.
    {
        const KernelCacheKey key{ContentHash{0x42}, RowHash{0x43}};
        std::array<std::uint8_t, 32> buf{};
        auto written = fed::serialize_federation_entry(
            buf, key, std::span<const std::uint8_t>{});
        ASSERT_TRUE(written.has_value());

        auto h = fed::deserialize_federation_header(buf, 6);
        auto e = fed::deserialize_federation_entry(buf, 6);
        ASSERT_TRUE(h.has_value());
        ASSERT_TRUE(e.has_value());
        assert(h->content_hash == e->header.content_hash);
        assert(h->row_hash     == e->header.row_hash);
    }

    std::printf("  test_header_overload_agreement:                 PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// Group 18 — federation_error_name covers every enum value.
// Exhaustive switch + non-empty + non-sentinel for every error.

static void test_error_name_coverage() {
    using E = fed::FederationError;
    constexpr E all[] = {
        E::None, E::BadMagic, E::UnsupportedVersion,
        E::UniverseCardinalityTooHigh, E::SentinelKey, E::ZeroKey,
        E::ReservedNonZero, E::TruncatedHeader, E::TruncatedPayload,
        E::OutputBufferTooSmall,
    };
    for (auto e : all) {
        const auto nm = fed::federation_error_name(e);
        assert(!nm.empty());
        assert(nm != "<unknown FederationError>");
    }

    // Specific spelling spot-checks.
    assert(fed::federation_error_name(E::None) == "None");
    assert(fed::federation_error_name(E::BadMagic) == "BadMagic");
    assert(fed::federation_error_name(E::UniverseCardinalityTooHigh)
           == "UniverseCardinalityTooHigh");

    std::printf("  test_error_name_coverage:                       PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// Group 19 — round-trip preserves arbitrary payload bytes (bit-stable
// across the codec).  Uses a 256-byte payload covering every byte
// value 0..255.

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

    auto view = fed::deserialize_federation_entry(
        std::span<const std::uint8_t>(buf.data(), *written),
        static_cast<std::uint16_t>(crucible::effects::OsUniverse::cardinality));
    ASSERT_TRUE(view.has_value());
    assert(view->payload.size() == payload.size());
    for (std::size_t i = 0; i < payload.size(); ++i) {
        assert(view->payload[i] == payload[i]);
    }

    std::printf("  test_round_trip_full_byte_range:                PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// Group 20 — view's payload span aliases the input buffer.  The
// payload range must point INTO in_buf, not into a copy.  This
// witness pins zero-copy semantics — federation receivers can read
// gigabyte payloads without allocating.

static void test_view_payload_aliases_input() {
    const KernelCacheKey key{ContentHash{0x42}, RowHash{0x43}};
    std::array<std::uint8_t, 8> payload = {1, 2, 3, 4, 5, 6, 7, 8};
    std::array<std::uint8_t, 64> buf{};
    auto written = fed::serialize_federation_entry(buf, key, payload);
    ASSERT_TRUE(written.has_value());

    auto view = fed::deserialize_federation_entry(
        std::span<const std::uint8_t>(buf.data(), *written),
        static_cast<std::uint16_t>(crucible::effects::OsUniverse::cardinality));
    ASSERT_TRUE(view.has_value());

    // The payload span starts at buf.data() + 32 (header bytes).
    assert(view->payload.data() == buf.data() + fed::FEDERATION_HEADER_BYTES);

    std::printf("  test_view_payload_aliases_input:                PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// Group 21 — strong-type cross-axis swap rejection.  A KernelCacheKey
// with content_hash and row_hash swapped (same raw values, different
// axes) MUST encode + decode to a DIFFERENT byte stream than the
// original — the wire format encodes which axis is which.

static void test_axis_swap_distinct_on_wire() {
    const KernelCacheKey k_normal{
        ContentHash{0x1111'1111'1111'1111ULL},
        RowHash{0x2222'2222'2222'2222ULL},
    };
    const KernelCacheKey k_swapped{
        ContentHash{0x2222'2222'2222'2222ULL},  // same raw value, but in content axis
        RowHash{0x1111'1111'1111'1111ULL},      // same raw value, but in row axis
    };

    std::array<std::uint8_t, 32> buf_n{};
    std::array<std::uint8_t, 32> buf_s{};
    auto wn = fed::serialize_federation_entry(
        buf_n, k_normal, std::span<const std::uint8_t>{});
    auto ws = fed::serialize_federation_entry(
        buf_s, k_swapped, std::span<const std::uint8_t>{});
    ASSERT_TRUE(wn.has_value());
    ASSERT_TRUE(ws.has_value());
    assert(*wn == *ws);  // same byte count
    assert(std::memcmp(buf_n.data(), buf_s.data(), *wn) != 0);  // different bytes

    std::printf("  test_axis_swap_distinct_on_wire:                PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// Group 22 — receiver_cardinality at the codec boundary lets us
// simulate "this receiver knows N atoms".  Pin that the parameter is
// NOT silently overridden by OsUniverse::cardinality at decode time.

static void test_receiver_cardinality_is_explicit() {
    const KernelCacheKey key{ContentHash{0x42}, RowHash{0x43}};
    std::array<std::uint8_t, 32> buf{};

    // Encode with stamp=10.
    fed::FederationEntryHeader hdr{};
    hdr.magic = fed::FEDERATION_MAGIC;
    hdr.protocol_version = fed::FEDERATION_PROTOCOL_V1;
    hdr.universe_cardinality = 10u;
    hdr.content_hash = key.content_hash;
    hdr.row_hash     = key.row_hash;
    hdr.payload_size = 0u;
    hdr.reserved     = 0u;
    std::memcpy(buf.data(), &hdr, sizeof(hdr));

    // Receiver cardinality 5 → reject.
    auto rej = fed::deserialize_federation_entry(buf, 5);
    ASSERT_TRUE(!rej.has_value());
    assert(rej.error() == fed::FederationError::UniverseCardinalityTooHigh);

    // Receiver cardinality 10 → accept.
    auto eq = fed::deserialize_federation_entry(buf, 10);
    ASSERT_TRUE(eq.has_value());

    // Receiver cardinality 100 → accept.
    auto big = fed::deserialize_federation_entry(buf, 100);
    ASSERT_TRUE(big.has_value());

    std::printf("  test_receiver_cardinality_is_explicit:          PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// Group 23 — noexcept witness.  Both codec entry points are noexcept;
// the std::expected error channel is the only failure mode.

static void test_codec_is_noexcept() {
    std::array<std::uint8_t, 64> buf{};
    const KernelCacheKey key{ContentHash{0x42}, RowHash{0x43}};
    static_assert(noexcept(fed::serialize_federation_entry(
        buf, key, std::span<const std::uint8_t>{})));
    static_assert(noexcept(fed::deserialize_federation_header(
        std::span<const std::uint8_t>{}, std::uint16_t{6})));
    static_assert(noexcept(fed::deserialize_federation_entry(
        std::span<const std::uint8_t>{}, std::uint16_t{6})));
    static_assert(noexcept(fed::federation_accepts_cardinality(
        std::uint16_t{6}, std::uint16_t{6})));
    static_assert(noexcept(fed::federation_error_name(
        fed::FederationError::None)));

    std::printf("  test_codec_is_noexcept:                         PASSED\n");
}

int main() {
    std::printf("test_federation_protocol — FOUND-I08 wire-format witness\n");
    test_header_layout_invariants();
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
    std::printf("test_federation_protocol: 23 groups, all passed\n");
    return 0;
}
