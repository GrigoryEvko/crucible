// FOUND-F12 — ComputationCache federation protocol.
//
// Composes F11's row-aware ComputationCache with I08's federation
// wire format.  Test surface verifies:
//
//   • InitSafe — federation_content_hash / federation_row_hash /
//                federation_key are constexpr; results are
//                fully-specified.
//   • TypeSafe — KernelCacheKey strong typing prevents axis swap;
//                IsCacheableFunction + IsEffectRow concept fences.
//   • DetSafe  — same (FnPtr, Row, Args...) → same key, byte-stable
//                within one build.
//   • Round-trip — serialize_*_federation_entry / deserialize_*_entry
//                  losslessly round-trip the (key, payload) pair.
//   • Cross-row distinctness on the wire — Row<>, Row<Bg>, Row<IO>,
//                Row<Bg, IO> produce DIFFERENT byte streams for the
//                same (FnPtr, Args...).
//   • Sentinel/zero rejection — federation_key never produces a
//                sentinel or all-zero key for valid inputs.
//
// Test groups (T01-T15):
//   T01 — federation_content_hash non-zero + non-sentinel
//   T02 — federation_row_hash non-zero (cardinality-seeded I02 invariant)
//   T03 — federation_key composes axes correctly
//   T04 — same (FnPtr, Row, Args...) → same key (deterministic)
//   T05 — different Row → different key (row-axis distinguishes)
//   T06 — different FnPtr → different key
//   T07 — different Args → different key
//   T08 — row permutation invariance (sort-fold I02 invariant)
//   T09 — empty Args round-trip via void-fn
//   T10 — codec round-trip: encode + decode bit-identical
//   T11 — cross-row on-wire distinctness (Row<> vs Row<Bg>)
//   T12 — federation_key never sentinel / never zero (4 row shapes)
//   T13 — composes-with-existing F11 cache key (key-into-row matches)
//   T14 — runtime smoke test (header-defined fixture)
//   T15 — view payload aliases input buffer (zero-copy semantics
//         inherited from I08)
//   T16 — content-addressed payload overload emits header-only hash
//         announcement when receiver already has the hash

#include <crucible/cipher/ComputationCacheFederation.h>
#include <crucible/cipher/ComputationCache.h>
#include <crucible/cipher/FederationProtocol.h>
#include <crucible/Types.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/Capabilities.h>

#include "test_assert.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <span>
#include <vector>

using namespace crucible;
namespace fed = crucible::cipher::federation;
namespace eff = crucible::effects;

#define ASSERT_TRUE(...) assert((__VA_ARGS__))

// ─────────────────────────────────────────────────────────────────────
// Local fixtures — distinct from header self-test fixtures so any TU
// running both incurs no symbol collision.

namespace {
inline void t_unary(int) noexcept {}
inline void t_binary(int, double) noexcept {}
inline void t_void() noexcept {}
inline void t_other(int) noexcept {}  // same signature as t_unary

using R0   = eff::Row<>;
using RBg  = eff::Row<eff::Effect::Bg>;
using RIO  = eff::Row<eff::Effect::IO>;
using RBgIO = eff::Row<eff::Effect::Bg, eff::Effect::IO>;
using RIOBg = eff::Row<eff::Effect::IO, eff::Effect::Bg>;
using RFull = eff::Row<eff::Effect::Alloc, eff::Effect::IO,
                        eff::Effect::Block, eff::Effect::Bg,
                        eff::Effect::Init, eff::Effect::Test>;

const crucible::permissions::LocalCipherPermission&
local_cipher_permission() {
    static const auto permission =
        crucible::safety::mint_permission_root<
            crucible::permissions::tag::LocalCipherTag>();
    return permission;
}
}  // namespace

// ─────────────────────────────────────────────────────────────────────
// T01 — federation_content_hash non-zero + non-sentinel.

static void test_t01_content_hash_well_formed() {
    static_assert(fed::federation_content_hash<&t_unary, R0, int>().raw() != 0);
    static_assert(fed::federation_content_hash<&t_binary, R0, int, double>().raw() != 0);
    static_assert(fed::federation_content_hash<&t_void, R0>().raw() != 0);

    constexpr auto h = fed::federation_content_hash<&t_unary, R0, int>();
    static_assert(h.raw() != ~std::uint64_t{0});  // not sentinel

    std::printf("  T01 content_hash_well_formed:                PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// T02 — federation_row_hash non-zero for ALL row shapes.

static void test_t02_row_hash_well_formed() {
    static_assert(fed::federation_row_hash<R0>().raw() != 0);
    static_assert(fed::federation_row_hash<RBg>().raw() != 0);
    static_assert(fed::federation_row_hash<RIO>().raw() != 0);
    static_assert(fed::federation_row_hash<RBgIO>().raw() != 0);
    static_assert(fed::federation_row_hash<RFull>().raw() != 0);

    // EmptyRow has the cardinality-seeded I02 fold's non-zero
    // guarantee — important enough to pin separately.
    constexpr auto er = fed::federation_row_hash<R0>();
    static_assert(er.raw() != 0);

    std::printf("  T02 row_hash_well_formed:                    PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// T03 — federation_key composes axes correctly.

static void test_t03_key_composes_axes() {
    constexpr auto k = fed::federation_key<&t_unary, R0, int>();
    static_assert(k.content_hash
                  == fed::federation_content_hash<&t_unary, R0, int>());
    static_assert(k.row_hash
                  == fed::federation_row_hash<R0>());

    std::printf("  T03 key_composes_axes:                       PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// T04 — deterministic: same inputs → same key across multiple
// instantiations.

static void test_t04_deterministic() {
    static_assert(fed::federation_key<&t_unary, R0, int>()
                  == fed::federation_key<&t_unary, R0, int>());
    static_assert(fed::federation_key<&t_binary, RBgIO, int, double>()
                  == fed::federation_key<&t_binary, RBgIO, int, double>());

    std::printf("  T04 deterministic:                           PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// T05 — different Row → different key (the load-bearing F12 invariant).

static void test_t05_row_distinguishes() {
    static_assert(fed::federation_key<&t_unary, R0, int>()
                  != fed::federation_key<&t_unary, RBg, int>());
    static_assert(fed::federation_key<&t_unary, R0, int>()
                  != fed::federation_key<&t_unary, RIO, int>());
    static_assert(fed::federation_key<&t_unary, RBg, int>()
                  != fed::federation_key<&t_unary, RIO, int>());
    static_assert(fed::federation_key<&t_unary, RBg, int>()
                  != fed::federation_key<&t_unary, RBgIO, int>());

    // Row hash specifically also distinguishes (not just composite key).
    static_assert(fed::federation_row_hash<R0>()
                  != fed::federation_row_hash<RBg>());
    static_assert(fed::federation_row_hash<RBg>()
                  != fed::federation_row_hash<RIO>());
    static_assert(fed::federation_row_hash<RBgIO>()
                  != fed::federation_row_hash<RFull>());

    std::printf("  T05 row_distinguishes:                       PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// T06 — different FnPtr → different key, even with same Row + Args.

static void test_t06_fnptr_distinguishes() {
    static_assert(fed::federation_key<&t_unary, R0, int>()
                  != fed::federation_key<&t_other, R0, int>());
    static_assert(fed::federation_key<&t_unary, R0, int>()
                  != fed::federation_key<&t_void, R0>());

    std::printf("  T06 fnptr_distinguishes:                     PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// T07 — different Args → different key.

static void test_t07_args_distinguishes() {
    static_assert(fed::federation_key<&t_unary, R0, int>()
                  != fed::federation_key<&t_unary, R0, float>());
    static_assert(fed::federation_key<&t_binary, R0, int, double>()
                  != fed::federation_key<&t_binary, R0, double, int>());

    std::printf("  T07 args_distinguishes:                      PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// T08 — row permutation invariance.

static void test_t08_row_permutation_invariance() {
    static_assert(fed::federation_row_hash<RBgIO>()
                  == fed::federation_row_hash<RIOBg>());
    static_assert(fed::federation_key<&t_unary, RBgIO, int>()
                  == fed::federation_key<&t_unary, RIOBg, int>());

    std::printf("  T08 row_permutation_invariance:              PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// T09 — empty Args round-trip (void-fn).

static void test_t09_empty_args_round_trip() {
    constexpr auto k_void_empty =
        fed::federation_key<&t_void, R0>();
    constexpr auto k_void_bg =
        fed::federation_key<&t_void, RBg>();
    static_assert(k_void_empty.content_hash.raw() != 0);
    static_assert(k_void_empty != k_void_bg);

    std::printf("  T09 empty_args_round_trip:                   PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// T10 — codec round-trip: encode + decode bit-identical.

static void test_t10_codec_round_trip() {
    const std::array<std::uint8_t, 16> body = {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
    };

    std::array<std::uint8_t, 64> buf{};
    auto written = fed::serialize_computation_cache_federation_entry<
        &t_unary, R0, int>(local_cipher_permission(), buf, body);
    ASSERT_TRUE(written.has_value());

    auto view = fed::deserialize_untrusted_federation_entry(
        std::span<const std::uint8_t>(buf.data(), *written),
        static_cast<std::uint16_t>(eff::OsUniverse::cardinality));
    ASSERT_TRUE(view.has_value());

    const auto expected_key = fed::federation_key<&t_unary, R0, int>();
    assert(view->header.content_hash == expected_key.content_hash);
    assert(view->header.row_hash     == expected_key.row_hash);
    assert(view->payload.size() == body.size());
    for (std::size_t i = 0; i < body.size(); ++i) {
        assert(view->payload[i] == body[i]);
    }

    std::printf("  T10 codec_round_trip:                        PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// T11 — cross-row on-wire distinctness.  Same FnPtr + Args, different
// Row → distinct byte streams (the row_hash field at offset 16..23
// differs).

static void test_t11_cross_row_on_wire_distinct() {
    std::array<std::uint8_t, 32> buf_r0{};
    std::array<std::uint8_t, 32> buf_rbg{};
    std::array<std::uint8_t, 32> buf_rio{};
    std::array<std::uint8_t, 32> buf_rbgio{};

    auto w0 = fed::serialize_computation_cache_federation_entry<
        &t_unary, R0, int>(local_cipher_permission(), buf_r0, std::span<const std::uint8_t>{});
    auto wb = fed::serialize_computation_cache_federation_entry<
        &t_unary, RBg, int>(local_cipher_permission(), buf_rbg, std::span<const std::uint8_t>{});
    auto wi = fed::serialize_computation_cache_federation_entry<
        &t_unary, RIO, int>(local_cipher_permission(), buf_rio, std::span<const std::uint8_t>{});
    auto wbi = fed::serialize_computation_cache_federation_entry<
        &t_unary, RBgIO, int>(local_cipher_permission(), buf_rbgio, std::span<const std::uint8_t>{});
    ASSERT_TRUE(w0.has_value());
    ASSERT_TRUE(wb.has_value());
    ASSERT_TRUE(wi.has_value());
    ASSERT_TRUE(wbi.has_value());

    // All four byte streams must differ pairwise.
    auto bufs_differ = [](const std::array<std::uint8_t, 32>& a,
                           const std::array<std::uint8_t, 32>& b) {
        for (std::size_t i = 0; i < 32; ++i) {
            if (a[i] != b[i]) return true;
        }
        return false;
    };
    assert(bufs_differ(buf_r0,   buf_rbg));
    assert(bufs_differ(buf_r0,   buf_rio));
    assert(bufs_differ(buf_r0,   buf_rbgio));
    assert(bufs_differ(buf_rbg,  buf_rio));
    assert(bufs_differ(buf_rbg,  buf_rbgio));
    assert(bufs_differ(buf_rio,  buf_rbgio));

    std::printf("  T11 cross_row_on_wire_distinct:              PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// T12 — federation_key never sentinel / never zero.

static void test_t12_key_never_sentinel_or_zero() {
    constexpr auto k0 = fed::federation_key<&t_unary, R0, int>();
    constexpr auto kb = fed::federation_key<&t_unary, RBg, int>();
    constexpr auto ki = fed::federation_key<&t_unary, RIO, int>();
    constexpr auto kf = fed::federation_key<&t_unary, RFull, int>();

    static_assert(!k0.is_zero());
    static_assert(!k0.is_sentinel());
    static_assert(!kb.is_zero());
    static_assert(!kb.is_sentinel());
    static_assert(!ki.is_zero());
    static_assert(!ki.is_sentinel());
    static_assert(!kf.is_zero());
    static_assert(!kf.is_sentinel());

    std::printf("  T12 key_never_sentinel_or_zero:              PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// T13 — federation_key composes with F11's computation_cache_key_in_row.
// The federation content hash MUST equal the F11 cache key cast to
// ContentHash; this pins the projection contract at the type level.

static void test_t13_composes_with_f11_cache_key() {
    constexpr auto fed_content =
        fed::federation_content_hash<&t_unary, RBg, int>().raw();
    constexpr auto f11_key =
        crucible::cipher::computation_cache_key_in_row<
            &t_unary, RBg, int>;
    static_assert(fed_content == f11_key,
        "F12 federation content hash MUST equal F11 "
        "computation_cache_key_in_row — the projection is the "
        "documented contract.");

    std::printf("  T13 composes_with_f11_cache_key:             PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// T14 — runtime smoke test from the header self-test block.

static void test_t14_header_smoke_test() {
    bool ok = fed::computation_cache_federation_smoke_test();
    assert(ok);
    std::printf("  T14 header_smoke_test:                       PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// T15 — payload aliasing.  Inherited from I08; pin here that the
// composition preserves the zero-copy contract.

static void test_t15_payload_aliases_input() {
    const std::array<std::uint8_t, 8> body = {1, 2, 3, 4, 5, 6, 7, 8};
    std::array<std::uint8_t, 64> buf{};
    auto written = fed::serialize_computation_cache_federation_entry<
        &t_unary, R0, int>(local_cipher_permission(), buf, body);
    ASSERT_TRUE(written.has_value());

    auto view = fed::deserialize_untrusted_federation_entry(
        std::span<const std::uint8_t>(buf.data(), *written),
        static_cast<std::uint16_t>(eff::OsUniverse::cardinality));
    ASSERT_TRUE(view.has_value());
    assert(view->payload.data() == buf.data() + fed::FEDERATION_HEADER_BYTES);

    std::printf("  T15 payload_aliases_input:                   PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// T16 — ContentAddressed federation payload.  The typed overload names
// ContentAddressed<FederationEntryPayload<KeyTag>> at compile time and
// keeps the runtime carrier span-sized.  hash_only() emits a header-only
// entry: content_hash + row_hash cross the wire, payload bytes do not.

static void test_t16_content_addressed_payload_elision() {
    using Payload =
        fed::ComputationCacheFederationContentAddressedPayload<
            &t_unary, R0, int>;
    static_assert(crucible::safety::proto::is_content_addressed_v<
        typename Payload::payload_type>);
    static_assert(sizeof(Payload) == sizeof(std::span<const std::uint8_t>));

    const std::array<std::uint8_t, 16> body = {
        0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7,
        0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF,
    };

    std::array<std::uint8_t, 64> full_buf{};
    auto full_written = fed::serialize_computation_cache_federation_entry<
        &t_unary, R0, int>(local_cipher_permission(), full_buf, Payload{body});
    ASSERT_TRUE(full_written.has_value());
    assert(*full_written == fed::FEDERATION_HEADER_BYTES + body.size());

    std::array<std::uint8_t, 64> hash_only_buf{};
    auto hash_only_written = fed::serialize_computation_cache_federation_entry<
        &t_unary, R0, int>(local_cipher_permission(), hash_only_buf, Payload::hash_only());
    ASSERT_TRUE(hash_only_written.has_value());
    assert(*hash_only_written == fed::FEDERATION_HEADER_BYTES);

    auto view = fed::deserialize_untrusted_federation_entry(
        std::span<const std::uint8_t>(hash_only_buf.data(), *hash_only_written),
        static_cast<std::uint16_t>(eff::OsUniverse::cardinality));
    ASSERT_TRUE(view.has_value());
    assert(view->payload.empty());
    assert((view->header.content_hash
            == fed::federation_key<&t_unary, R0, int>().content_hash));
    assert((view->header.row_hash
            == fed::federation_key<&t_unary, R0, int>().row_hash));

    std::printf("  T16 content_addressed_payload_elision:       PASSED\n");
}

// ═════════════════════════════════════════════════════════════════════
// ── FOUND-F12-AUDIT — per-axis isolation + wire-byte fences ────────
// ═════════════════════════════════════════════════════════════════════
//
// Five audit groups closing gaps in the base T01-T15 surface:
//
//   AUDIT-A — content axis is row-aware INDEPENDENT of the row axis.
//             T05 only proves the COMPOSITE key differs across rows;
//             this group isolates each axis's contribution.
//   AUDIT-B — wire-byte-offset stability.  Pin that content_hash
//             occupies bytes [8..15] and row_hash occupies bytes
//             [16..23] of the I08 32-byte header, little-endian.
//             Without this, a refactor of the header layout could
//             silently shift fields and break cross-version
//             compatibility.
//   AUDIT-C — cross-Universe-cardinality receiver rejection.
//             Inherits I04 append-only discipline through F12: a
//             receiver with a smaller universe must reject entries
//             written from a larger universe.
//   AUDIT-D — row-permutation byte-level invariance.  T08 proves
//             keys equal under row permutation; this group proves
//             the WIRE bytes are byte-identical.
//   AUDIT-E — saturation-row (RFull = 6 atoms) round-trip + key
//             well-formedness.

// ─────────────────────────────────────────────────────────────────────
// AUDIT-A — content axis row-aware in isolation.
//
// federation_content_hash<&f, R, Args...> ALONE must differ across
// rows even though we don't compare the row_hash axis.  This pins
// that F11's `computation_cache_key_in_row` mixes the row hash into
// the content axis (the load-bearing slot-isolation invariant), not
// just relying on the row axis to disambiguate.

static void test_audit_a_content_axis_row_isolation() {
    static_assert(fed::federation_content_hash<&t_unary, R0, int>()
                  != fed::federation_content_hash<&t_unary, RBg, int>(),
        "F12-AUDIT-A: content hash MUST differ across rows even when "
        "the row axis is read separately — the F11 in_row fold mixes "
        "row contribution into the content key.");
    static_assert(fed::federation_content_hash<&t_unary, RBg, int>()
                  != fed::federation_content_hash<&t_unary, RIO, int>());
    static_assert(fed::federation_content_hash<&t_unary, RBgIO, int>()
                  != fed::federation_content_hash<&t_unary, RFull, int>());

    // Conversely, content hash MUST match under row permutation
    // (sort-fold inheritance from I02 through F11).
    static_assert(fed::federation_content_hash<&t_unary, RBgIO, int>()
                  == fed::federation_content_hash<&t_unary, RIOBg, int>(),
        "F12-AUDIT-A: content hash inherits row-permutation invariance "
        "from F11+I02 sort-fold.");

    std::printf("  AUDIT-A content_axis_row_isolation:          PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// AUDIT-B — wire-byte-offset stability.
//
// Per the I08 spec at FederationProtocol.h:21-27, the 32-byte
// header has:
//   offset 0..3   magic ('CFED' = 0x44454643u, little-endian)
//   offset 4..5   protocol_version
//   offset 6..7   universe_cardinality
//   offset 8..15  content_hash
//   offset 16..23 row_hash
//   offset 24..27 payload_size
//   offset 28..31 reserved (must be 0)
//
// This group memcpy's bytes from the serialized buffer and compares
// them to the projected hashes — if a refactor moves a field, this
// fires immediately.

static void test_audit_b_wire_byte_offset_stability() {
    constexpr auto k = fed::federation_key<&t_unary, RBgIO, int>();
    const auto expected_content = k.content_hash.raw();
    const auto expected_row     = k.row_hash.raw();

    std::array<std::uint8_t, 64> buf{};
    auto written = fed::serialize_computation_cache_federation_entry<
        &t_unary, RBgIO, int>(local_cipher_permission(), buf, std::span<const std::uint8_t>{});
    ASSERT_TRUE(written.has_value());
    ASSERT_TRUE(*written == fed::FEDERATION_HEADER_BYTES);  // empty payload

    // Read 8 bytes at offset 8 and compare to expected content hash.
    std::uint64_t observed_content = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        observed_content |= static_cast<std::uint64_t>(buf[8 + i])
                            << (i * 8);
    }
    assert(observed_content == expected_content);

    // Read 8 bytes at offset 16 and compare to expected row hash.
    std::uint64_t observed_row = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        observed_row |= static_cast<std::uint64_t>(buf[16 + i])
                        << (i * 8);
    }
    assert(observed_row == expected_row);

    // Magic at offset 0..3.
    std::uint32_t observed_magic = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        observed_magic |= static_cast<std::uint32_t>(buf[i]) << (i * 8);
    }
    assert(observed_magic == fed::FEDERATION_MAGIC);

    std::printf("  AUDIT-B wire_byte_offset_stability:          PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// AUDIT-C — cross-Universe-cardinality receiver rejection.
//
// FOUND-I04 invariant: Universe.cardinality is append-only — atoms
// only get added, never removed or reordered.  The federation
// protocol stamps the writer's cardinality in the header at offset
// 6..7.  A receiver with a SMALLER cardinality cannot understand
// entries whose universe_cardinality field exceeds its own — those
// entries might mention atoms the receiver hasn't yet defined.  The
// I08 codec rejects such entries with FederationError::
// UniverseCardinalityTooHigh.
//
// This audit pins that the F12 bridge inherits this rejection: a
// receiver claiming `cardinality - 1` MUST reject an entry written
// at the current cardinality, even though F12 itself doesn't
// directly write the cardinality field (it's stamped by the I08
// serialize path the bridge calls into).

static void test_audit_c_cross_universe_cardinality_rejection() {
    constexpr auto current_cardinality =
        static_cast<std::uint16_t>(eff::OsUniverse::cardinality);
    static_assert(current_cardinality >= 1u,
        "F12-AUDIT-C: this fixture requires cardinality >= 1 to "
        "construct a strictly-smaller receiver cardinality.");

    std::array<std::uint8_t, 64> buf{};
    const std::array<std::uint8_t, 4> body = {0xAA, 0xBB, 0xCC, 0xDD};

    auto written = fed::serialize_computation_cache_federation_entry<
        &t_unary, RFull, int>(local_cipher_permission(), buf, body);
    ASSERT_TRUE(written.has_value());

    // Receiver at current cardinality — should accept.
    {
        auto view = fed::deserialize_untrusted_federation_entry(
            std::span<const std::uint8_t>(buf.data(), *written),
            current_cardinality);
        ASSERT_TRUE(view.has_value());
    }

    // Receiver at cardinality - 1 — must reject.
    {
        auto view = fed::deserialize_untrusted_federation_entry(
            std::span<const std::uint8_t>(buf.data(), *written),
            static_cast<std::uint16_t>(current_cardinality - 1u));
        ASSERT_TRUE(!view.has_value());
        assert(view.error()
               == crucible::cipher::federation::FederationError::
                      UniverseCardinalityTooHigh);
    }

    std::printf("  AUDIT-C cross_universe_cardinality_rejection: PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// AUDIT-D — row-permutation byte-level invariance.
//
// T08 proves federation_key<&fn, Row<Bg,IO>, int>() ==
//                federation_key<&fn, Row<IO,Bg>, int>().
// This audit goes further: the SERIALIZED BYTES must be identical
// too — same magic + version + cardinality + content_hash +
// row_hash + payload_size + reserved + payload.  If a refactor
// were to make the codec sensitive to template-arg order beyond
// what the key projection exposes, the wire bytes would drift even
// with identical keys.

static void test_audit_d_row_permutation_byte_invariance() {
    const std::array<std::uint8_t, 8> body = {0x10, 0x20, 0x30, 0x40,
                                                0x50, 0x60, 0x70, 0x80};

    std::array<std::uint8_t, 64> buf_bgio{};
    std::array<std::uint8_t, 64> buf_iobg{};

    auto wa = fed::serialize_computation_cache_federation_entry<
        &t_unary, RBgIO, int>(local_cipher_permission(), buf_bgio, body);
    auto wb = fed::serialize_computation_cache_federation_entry<
        &t_unary, RIOBg, int>(local_cipher_permission(), buf_iobg, body);
    ASSERT_TRUE(wa.has_value());
    ASSERT_TRUE(wb.has_value());
    assert(*wa == *wb);

    // Byte-for-byte equality across the entire written region.
    for (std::size_t i = 0; i < *wa; ++i) {
        assert(buf_bgio[i] == buf_iobg[i]);
    }

    std::printf("  AUDIT-D row_permutation_byte_invariance:     PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// AUDIT-E — saturation-row (RFull = 6 atoms) round-trip.
//
// RFull = Row<Alloc, IO, Block, Bg, Init, Test> exercises every
// Effect atom in the current OsUniverse.  Pins that:
//   1. The federation key for RFull is well-formed (non-zero,
//      non-sentinel).
//   2. RFull row hash differs from each single-atom row.
//   3. Round-trip preserves both axes.

static void test_audit_e_saturation_row_round_trip() {
    constexpr auto k_full = fed::federation_key<&t_unary, RFull, int>();
    static_assert(!k_full.is_zero());
    static_assert(!k_full.is_sentinel());
    static_assert(k_full.row_hash.raw() != 0);

    static_assert(fed::federation_row_hash<RFull>()
                  != fed::federation_row_hash<R0>());
    static_assert(fed::federation_row_hash<RFull>()
                  != fed::federation_row_hash<RBg>());
    static_assert(fed::federation_row_hash<RFull>()
                  != fed::federation_row_hash<RIO>());
    static_assert(fed::federation_row_hash<RFull>()
                  != fed::federation_row_hash<RBgIO>());

    const std::array<std::uint8_t, 12> body = {0xDE, 0xAD, 0xBE, 0xEF,
                                                 0xCA, 0xFE, 0xBA, 0xBE,
                                                 0x01, 0x02, 0x03, 0x04};
    std::array<std::uint8_t, 64> buf{};
    auto written = fed::serialize_computation_cache_federation_entry<
        &t_unary, RFull, int>(local_cipher_permission(), buf, body);
    ASSERT_TRUE(written.has_value());

    auto view = fed::deserialize_untrusted_federation_entry(
        std::span<const std::uint8_t>(buf.data(), *written),
        static_cast<std::uint16_t>(eff::OsUniverse::cardinality));
    ASSERT_TRUE(view.has_value());
    assert(view->header.content_hash == k_full.content_hash);
    assert(view->header.row_hash     == k_full.row_hash);
    assert(view->payload.size() == body.size());
    for (std::size_t i = 0; i < body.size(); ++i) {
        assert(view->payload[i] == body[i]);
    }

    std::printf("  AUDIT-E saturation_row_round_trip:           PASSED\n");
}

int main() {
    std::printf("test_computation_cache_federation — FOUND-F12 bridge\n");
    test_t01_content_hash_well_formed();
    test_t02_row_hash_well_formed();
    test_t03_key_composes_axes();
    test_t04_deterministic();
    test_t05_row_distinguishes();
    test_t06_fnptr_distinguishes();
    test_t07_args_distinguishes();
    test_t08_row_permutation_invariance();
    test_t09_empty_args_round_trip();
    test_t10_codec_round_trip();
    test_t11_cross_row_on_wire_distinct();
    test_t12_key_never_sentinel_or_zero();
    test_t13_composes_with_f11_cache_key();
    test_t14_header_smoke_test();
    test_t15_payload_aliases_input();
    test_t16_content_addressed_payload_elision();
    test_audit_a_content_axis_row_isolation();
    test_audit_b_wire_byte_offset_stability();
    test_audit_c_cross_universe_cardinality_rejection();
    test_audit_d_row_permutation_byte_invariance();
    test_audit_e_saturation_row_round_trip();
    std::printf("test_computation_cache_federation: 21 groups, all passed\n");
    return 0;
}
