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
        &t_unary, R0, int>(buf, body);
    ASSERT_TRUE(written.has_value());

    auto view = fed::deserialize_federation_entry(
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
        &t_unary, R0, int>(buf_r0, std::span<const std::uint8_t>{});
    auto wb = fed::serialize_computation_cache_federation_entry<
        &t_unary, RBg, int>(buf_rbg, std::span<const std::uint8_t>{});
    auto wi = fed::serialize_computation_cache_federation_entry<
        &t_unary, RIO, int>(buf_rio, std::span<const std::uint8_t>{});
    auto wbi = fed::serialize_computation_cache_federation_entry<
        &t_unary, RBgIO, int>(buf_rbgio, std::span<const std::uint8_t>{});
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
        &t_unary, R0, int>(buf, body);
    ASSERT_TRUE(written.has_value());

    auto view = fed::deserialize_federation_entry(
        std::span<const std::uint8_t>(buf.data(), *written),
        static_cast<std::uint16_t>(eff::OsUniverse::cardinality));
    ASSERT_TRUE(view.has_value());
    assert(view->payload.data() == buf.data() + fed::FEDERATION_HEADER_BYTES);

    std::printf("  T15 payload_aliases_input:                   PASSED\n");
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
    std::printf("test_computation_cache_federation: 15 groups, all passed\n");
    return 0;
}
