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
#include <crucible/Serialize.h>           // FOUND-I08-AUDIT (Finding G): CDAG_MAGIC
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

// ═════════════════════════════════════════════════════════════════════
// FOUND-I08-AUDIT — additional rigor pass
// ═════════════════════════════════════════════════════════════════════

// ─────────────────────────────────────────────────────────────────────
// Audit Group A — UINT16_MAX cardinality boundary.  The 16-bit field
// supports up to 65535 atoms; the predicate + codec must handle the
// extreme without arithmetic surprise.

static void test_audit_a_cardinality_boundary_uint16_max() {
    constexpr std::uint16_t MAX_CARD = 0xFFFFu;

    // Predicate (consteval-friendly).
    static_assert( fed::federation_accepts_cardinality(MAX_CARD, MAX_CARD));
    static_assert( fed::federation_accepts_cardinality(0, MAX_CARD));
    static_assert(!fed::federation_accepts_cardinality(MAX_CARD, MAX_CARD - 1));

    // Wire-format witness: encode-with-stamp(MAX) + decode-with-MAX.
    std::array<std::uint8_t, 32> buf{};
    fed::FederationEntryHeader hdr{};
    hdr.magic = fed::FEDERATION_MAGIC;
    hdr.protocol_version = fed::FEDERATION_PROTOCOL_V1;
    hdr.universe_cardinality = MAX_CARD;
    hdr.content_hash = ContentHash{0x42};
    hdr.row_hash     = RowHash{0x43};
    hdr.payload_size = 0u;
    hdr.reserved     = 0u;
    std::memcpy(buf.data(), &hdr, sizeof(hdr));

    auto accept = fed::deserialize_federation_entry(buf, MAX_CARD);
    ASSERT_TRUE(accept.has_value());
    assert(accept->header.universe_cardinality == MAX_CARD);

    // Equal-MAX-MAX boundary case (both ends).
    auto reject_one_below =
        fed::deserialize_federation_entry(buf, MAX_CARD - 1u);
    ASSERT_TRUE(!reject_one_below.has_value());
    assert(reject_one_below.error() ==
           fed::FederationError::UniverseCardinalityTooHigh);

    std::printf("  [AUDIT-A] cardinality_boundary_uint16_max:      PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// Audit Group B — buffer-exactly-fits acceptance.  serialize must
// accept an out_buf whose size equals header + payload exactly (no
// slack).  The boundary case is tested explicitly because off-by-one
// in the size check would produce a silent "OutputBufferTooSmall"
// for buffers that should have worked.

static void test_audit_b_buffer_exactly_fits() {
    const KernelCacheKey key{
        ContentHash{0x1111'2222'3333'4444ULL},
        RowHash{0x5555'6666'7777'8888ULL},
    };
    const std::array<std::uint8_t, 16> payload = {
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16
    };

    // out_buf sized EXACTLY to header + payload.
    std::array<std::uint8_t, 48> buf{};  // 32 + 16
    auto written = fed::serialize_federation_entry(buf, key, payload);
    ASSERT_TRUE(written.has_value());
    assert(*written == 48u);

    // out_buf sized 1 byte short → reject.
    std::array<std::uint8_t, 47> short_buf{};
    auto rejected =
        fed::serialize_federation_entry(short_buf, key, payload);
    ASSERT_TRUE(!rejected.has_value());
    assert(rejected.error() ==
           fed::FederationError::OutputBufferTooSmall);

    // Empty payload, out_buf exactly 32 bytes → accept.
    std::array<std::uint8_t, 32> tight_buf{};
    auto tight =
        fed::serialize_federation_entry(tight_buf, key,
                                         std::span<const std::uint8_t>{});
    ASSERT_TRUE(tight.has_value());
    assert(*tight == 32u);

    std::printf("  [AUDIT-B] buffer_exactly_fits:                   PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// Audit Group C — extra bytes at end are ignored (zero-copy aliasing
// semantics witness).  A federation transport that batches multiple
// entries into one byte buffer would pass a buffer with leftover
// bytes after the first entry; deserialize_federation_entry must
// return a payload span that caps at header.payload_size, NOT the
// full remaining bytes.

static void test_audit_c_extra_bytes_at_end() {
    const KernelCacheKey key{ContentHash{0x42}, RowHash{0x43}};
    const std::array<std::uint8_t, 4> payload = {0xAA, 0xBB, 0xCC, 0xDD};

    // First serialize a 36-byte entry into a 64-byte buffer.
    std::array<std::uint8_t, 64> buf{};
    auto written = fed::serialize_federation_entry(buf, key, payload);
    ASSERT_TRUE(written.has_value());
    assert(*written == 36u);

    // Fill the leftover bytes with sentinel data — these must NOT
    // appear in the payload span.
    for (std::size_t i = 36; i < buf.size(); ++i) {
        buf[i] = 0xEE;
    }

    // Deserialize using the FULL 64-byte buffer (not just the 36
    // bytes we wrote).
    auto view = fed::deserialize_federation_entry(
        buf, static_cast<std::uint16_t>(
            crucible::effects::OsUniverse::cardinality));
    ASSERT_TRUE(view.has_value());
    assert(view->header.payload_size == 4u);
    assert(view->payload.size() == 4u);  // exactly payload_size, not 32
    assert(view->payload[0] == 0xAA);
    assert(view->payload[1] == 0xBB);
    assert(view->payload[2] == 0xCC);
    assert(view->payload[3] == 0xDD);
    // Extra bytes 0xEE never appear in the view.

    std::printf("  [AUDIT-C] extra_bytes_at_end:                    PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// Audit Group D — partial-sentinel acceptance.  ONLY the FULL
// sentinel (both axes UINT64_MAX) is rejected.  A key with one axis
// at UINT64_MAX and the other at a real hash IS a valid federation
// key — astronomically unlikely under FNV/fmix64 avalanche, but not
// structurally forbidden.

static void test_audit_d_partial_sentinel_accepted() {
    constexpr std::uint64_t MAX = std::numeric_limits<std::uint64_t>::max();

    // (a) content_hash = UINT64_MAX, row_hash = real → accept.
    {
        const KernelCacheKey key{
            ContentHash{MAX},
            RowHash{0xAAAA'BBBB'CCCC'DDDDULL},
        };
        assert(!key.is_sentinel());  // partial sentinel != full
        assert(!key.is_zero());

        std::array<std::uint8_t, 32> buf{};
        auto written = fed::serialize_federation_entry(
            buf, key, std::span<const std::uint8_t>{});
        ASSERT_TRUE(written.has_value());

        auto view = fed::deserialize_federation_entry(
            buf, static_cast<std::uint16_t>(
                crucible::effects::OsUniverse::cardinality));
        ASSERT_TRUE(view.has_value());
        assert(view->header.content_hash.raw() == MAX);
    }

    // (b) row_hash = UINT64_MAX, content_hash = real → accept.
    {
        const KernelCacheKey key{
            ContentHash{0x1234'5678'9ABC'DEF0ULL},
            RowHash{MAX},
        };
        assert(!key.is_sentinel());
        assert(!key.is_zero());

        std::array<std::uint8_t, 32> buf{};
        auto written = fed::serialize_federation_entry(
            buf, key, std::span<const std::uint8_t>{});
        ASSERT_TRUE(written.has_value());

        auto view = fed::deserialize_federation_entry(
            buf, static_cast<std::uint16_t>(
                crucible::effects::OsUniverse::cardinality));
        ASSERT_TRUE(view.has_value());
        assert(view->header.row_hash.raw() == MAX);
    }

    // (c) Partial-zero (one axis 0, other axis real) → accept.
    // is_zero() requires BOTH axes 0; partial zero is a real key.
    {
        const KernelCacheKey key{
            ContentHash{0u},
            RowHash{0xDEAD'BEEFULL},
        };
        assert(!key.is_zero());
        assert(!key.is_sentinel());

        std::array<std::uint8_t, 32> buf{};
        auto written = fed::serialize_federation_entry(
            buf, key, std::span<const std::uint8_t>{});
        ASSERT_TRUE(written.has_value());
    }

    std::printf("  [AUDIT-D] partial_sentinel_accepted:             PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// Audit Group E — same bit pattern in both axes.  A real federation
// key MAY have content_hash == row_hash (same 64 bits); the codec
// must round-trip this losslessly without confusing the axes.

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

    auto view = fed::deserialize_federation_entry(
        std::span<const std::uint8_t>(buf.data(), *written),
        static_cast<std::uint16_t>(crucible::effects::OsUniverse::cardinality));
    ASSERT_TRUE(view.has_value());
    assert(view->header.content_hash.raw() == SHARED_BITS);
    assert(view->header.row_hash.raw()     == SHARED_BITS);
    assert(view->header.content_hash       == key.content_hash);
    assert(view->header.row_hash           == key.row_hash);

    // Pin the byte-position discipline by inspecting the buffer:
    // bytes [8..15] should equal bytes [16..23] when content == row.
    for (std::size_t i = 0; i < 8; ++i) {
        assert(buf[8 + i] == buf[16 + i]);
    }

    std::printf("  [AUDIT-E] same_bit_pattern_axes:                 PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// Audit Group F — constexpr witness for predicate + error_name.
// Pin via static_assert at namespace scope that both functions can
// be evaluated in a constant-expression context.  The codec
// (serialize/deserialize) is intentionally NOT constexpr (uses
// std::memcpy on a buffer, which is runtime-only); only the
// query-side predicates fold at compile time.

namespace audit_f_constexpr_witnesses {
    // federation_accepts_cardinality is consteval-friendly.
    static_assert(fed::federation_accepts_cardinality(0, 0));
    static_assert(fed::federation_accepts_cardinality(6, 6));
    static_assert(!fed::federation_accepts_cardinality(7, 6));

    // federation_error_name is consteval-friendly.
    static_assert(fed::federation_error_name(fed::FederationError::None)
                  == "None");
    static_assert(fed::federation_error_name(fed::FederationError::BadMagic)
                  == "BadMagic");
    static_assert(fed::federation_error_name(
                      fed::FederationError::UniverseCardinalityTooHigh)
                  == "UniverseCardinalityTooHigh");

    // Constants are constexpr-reachable.
    static_assert(fed::FEDERATION_MAGIC == 0x44454643u);
    static_assert(fed::FEDERATION_PROTOCOL_V1 == 1u);
    static_assert(fed::FEDERATION_HEADER_BYTES == 32u);
}

static void test_audit_f_constexpr_witnesses() {
    // Runtime peer to ensure the namespace-scope static_asserts above
    // are reachable at runtime too (header inclusion fence — if the
    // header gets edited to make these non-constexpr, the static_asserts
    // fire AND the runtime test stops compiling).
    [[maybe_unused]] auto card_check =
        fed::federation_accepts_cardinality(6, 6);
    [[maybe_unused]] auto name_check =
        fed::federation_error_name(fed::FederationError::None);
    assert(card_check);
    assert(name_check == "None");

    std::printf("  [AUDIT-F] constexpr_witnesses:                   PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// Audit Group G — magic-collision guard with CDAG_MAGIC.  If a future
// refactor reassigns CDAG_MAGIC (Serialize.h:27) to FEDERATION_MAGIC,
// a federation byte stream and a Merkle DAG snapshot would silently
// dispatch to the wrong codec.  Pin the inequality as a runtime
// witness that pulls in BOTH headers.

static void test_audit_g_magic_collision_with_cdag() {
    // Both magics are namespace-distinct constants.
    static_assert(fed::FEDERATION_MAGIC == 0x44454643u);
    static_assert(crucible::CDAG_MAGIC  == 0x43444147u);
    static_assert(fed::FEDERATION_MAGIC != crucible::CDAG_MAGIC,
        "FEDERATION_MAGIC must not collide with CDAG_MAGIC.");

    // ASCII spelling check: 'CFED' vs 'GDAG' — distinct first byte
    // ensures an early-truncate magic-check rejects the wrong codec
    // immediately rather than after a full 4-byte compare.
    constexpr auto fed_first_byte =
        static_cast<std::uint8_t>(fed::FEDERATION_MAGIC & 0xFFu);
    constexpr auto cdag_first_byte =
        static_cast<std::uint8_t>(crucible::CDAG_MAGIC & 0xFFu);
    static_assert(fed_first_byte == 'C');
    static_assert(cdag_first_byte == 'G');
    static_assert(fed_first_byte != cdag_first_byte);

    std::printf("  [AUDIT-G] magic_collision_with_cdag:             PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// Audit Group H — header field-width pin (mirror header static_asserts
// at runtime so the doc-comment is reachable from a debugger).

static void test_audit_h_field_width_pins() {
    static_assert(sizeof(fed::FederationEntryHeader::magic)                == 4);
    static_assert(sizeof(fed::FederationEntryHeader::protocol_version)     == 2);
    static_assert(sizeof(fed::FederationEntryHeader::universe_cardinality) == 2);
    static_assert(sizeof(fed::FederationEntryHeader::content_hash)         == 8);
    static_assert(sizeof(fed::FederationEntryHeader::row_hash)             == 8);
    static_assert(sizeof(fed::FederationEntryHeader::payload_size)         == 4);
    static_assert(sizeof(fed::FederationEntryHeader::reserved)             == 4);

    // Sum = 32 (no padding).
    static_assert(4 + 2 + 2 + 8 + 8 + 4 + 4 == 32);

    std::printf("  [AUDIT-H] field_width_pins:                      PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// Audit Group I — full round-trip via std::vector buffer with
// alternating-byte payload pattern (catches off-by-one in offset
// arithmetic that would shift bytes).

static void test_audit_i_vector_buffer_roundtrip() {
    const KernelCacheKey key{
        ContentHash{0x0F0F'0F0F'0F0F'0F0FULL},
        RowHash{0xF0F0'F0F0'F0F0'F0F0ULL},
    };

    // 1024-byte alternating-bit payload — 0x55, 0xAA, 0x55, 0xAA, ...
    std::vector<std::uint8_t> payload(1024);
    for (std::size_t i = 0; i < payload.size(); ++i) {
        payload[i] = (i & 1u) ? 0xAAu : 0x55u;
    }

    std::vector<std::uint8_t> buf(fed::FEDERATION_HEADER_BYTES + payload.size());
    auto written = fed::serialize_federation_entry(buf, key, payload);
    ASSERT_TRUE(written.has_value());
    assert(*written == buf.size());

    auto view = fed::deserialize_federation_entry(
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
    std::printf("--- FOUND-I08-AUDIT ---\n");
    test_audit_a_cardinality_boundary_uint16_max();
    test_audit_b_buffer_exactly_fits();
    test_audit_c_extra_bytes_at_end();
    test_audit_d_partial_sentinel_accepted();
    test_audit_e_same_bit_pattern_axes();
    test_audit_f_constexpr_witnesses();
    test_audit_g_magic_collision_with_cdag();
    test_audit_h_field_width_pins();
    test_audit_i_vector_buffer_roundtrip();
    std::printf("test_federation_protocol: 23 + 9 audit groups, all passed\n");
    return 0;
}
