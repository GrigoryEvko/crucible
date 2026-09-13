#include <crucible/cipher/ComputationCacheFederation.h>
#include <crucible/cipher/ComputationCache.h>
#include <crucible/cipher/FederationProtocol.h>
#include <crucible/Types.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/safety/Stale.h>
#include <crucible/safety/Tagged.h>
#include <crucible/safety/diag/CanonicalOrder.h>

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

// These fixtures carry their own names rather than the ones the header's
// self-test uses, so a translation unit that runs both has no collision.

namespace {
inline void t_unary(int) noexcept {}
inline void t_binary(int, double) noexcept {}
inline void t_void() noexcept {}
inline void t_other(int) noexcept {}  // same signature as t_unary

using R0 = eff::Row<>;
using RBg = eff::Row<eff::Effect::Bg>;
using RIO = eff::Row<eff::Effect::IO>;
using RBgIO = eff::Row<eff::Effect::Bg, eff::Effect::IO>;
using RIOBg = eff::Row<eff::Effect::IO, eff::Effect::Bg>;
using RFull = eff::Row<eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block, eff::Effect::Bg, eff::Effect::Init,
                       eff::Effect::Test>;

const crucible::permissions::LocalCipherPermission& local_cipher_permission() {
    static const auto permission = crucible::safety::mint_permission_root<crucible::permissions::tag::LocalCipherTag>();
    return permission;
}
}  // namespace

static void test_t01_content_hash_well_formed() {
    static_assert(fed::federation_content_hash<&t_unary, R0, int>().raw() != 0);
    static_assert(fed::federation_content_hash<&t_binary, R0, int, double>().raw() != 0);
    static_assert(fed::federation_content_hash<&t_void, R0>().raw() != 0);

    constexpr auto h = fed::federation_content_hash<&t_unary, R0, int>();
    static_assert(h.raw() != ~std::uint64_t{0});  // all-ones is the sentinel

    std::printf("  T01 content_hash_well_formed:                PASSED\n");
}

static void test_t02_row_hash_well_formed() {
    static_assert(fed::federation_row_hash<R0>().raw() != 0);
    static_assert(fed::federation_row_hash<RBg>().raw() != 0);
    static_assert(fed::federation_row_hash<RIO>().raw() != 0);
    static_assert(fed::federation_row_hash<RBgIO>().raw() != 0);
    static_assert(fed::federation_row_hash<RFull>().raw() != 0);

    // The empty row is the case worth pinning on its own: the fold seeds on
    // the row cardinality, so even a row of no atoms hashes non-zero.
    constexpr auto er = fed::federation_row_hash<R0>();
    static_assert(er.raw() != 0);

    std::printf("  T02 row_hash_well_formed:                    PASSED\n");
}

static void test_t03_key_composes_axes() {
    constexpr auto k = fed::federation_key<&t_unary, R0, int>();
    static_assert(k.content_hash == fed::federation_content_hash<&t_unary, R0, int>());
    static_assert(k.row_hash == fed::federation_row_hash<R0>());

    std::printf("  T03 key_composes_axes:                       PASSED\n");
}

static void test_t04_deterministic() {
    static_assert(fed::federation_key<&t_unary, R0, int>() == fed::federation_key<&t_unary, R0, int>());
    static_assert(fed::federation_key<&t_binary, RBgIO, int, double>()
                  == fed::federation_key<&t_binary, RBgIO, int, double>());

    std::printf("  T04 deterministic:                           PASSED\n");
}

static void test_t05_row_distinguishes() {
    static_assert(fed::federation_key<&t_unary, R0, int>() != fed::federation_key<&t_unary, RBg, int>());
    static_assert(fed::federation_key<&t_unary, R0, int>() != fed::federation_key<&t_unary, RIO, int>());
    static_assert(fed::federation_key<&t_unary, RBg, int>() != fed::federation_key<&t_unary, RIO, int>());
    static_assert(fed::federation_key<&t_unary, RBg, int>() != fed::federation_key<&t_unary, RBgIO, int>());

    // The row axis distinguishes on its own, not only inside the composite.
    static_assert(fed::federation_row_hash<R0>() != fed::federation_row_hash<RBg>());
    static_assert(fed::federation_row_hash<RBg>() != fed::federation_row_hash<RIO>());
    static_assert(fed::federation_row_hash<RBgIO>() != fed::federation_row_hash<RFull>());

    std::printf("  T05 row_distinguishes:                       PASSED\n");
}

static void test_t06_fnptr_distinguishes() {
    static_assert(fed::federation_key<&t_unary, R0, int>() != fed::federation_key<&t_other, R0, int>());
    static_assert(fed::federation_key<&t_unary, R0, int>() != fed::federation_key<&t_void, R0>());

    std::printf("  T06 fnptr_distinguishes:                     PASSED\n");
}

static void test_t07_args_distinguishes() {
    static_assert(fed::federation_key<&t_unary, R0, int>() != fed::federation_key<&t_unary, R0, float>());
    static_assert(fed::federation_key<&t_binary, R0, int, double>()
                  != fed::federation_key<&t_binary, R0, double, int>());

    std::printf("  T07 args_distinguishes:                      PASSED\n");
}

static void test_t08_row_permutation_invariance() {
    static_assert(fed::federation_row_hash<RBgIO>() == fed::federation_row_hash<RIOBg>());
    static_assert(fed::federation_key<&t_unary, RBgIO, int>() == fed::federation_key<&t_unary, RIOBg, int>());

    std::printf("  T08 row_permutation_invariance:              PASSED\n");
}

static void test_t09_empty_args_round_trip() {
    constexpr auto k_void_empty = fed::federation_key<&t_void, R0>();
    constexpr auto k_void_bg = fed::federation_key<&t_void, RBg>();
    static_assert(k_void_empty.content_hash.raw() != 0);
    static_assert(k_void_empty != k_void_bg);

    std::printf("  T09 empty_args_round_trip:                   PASSED\n");
}

static void test_t10_codec_round_trip() {
    const std::array<std::uint8_t, 16> body = {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
    };

    std::array<std::uint8_t, 64> buf{};
    auto written =
        fed::serialize_computation_cache_federation_entry<&t_unary, R0, int>(local_cipher_permission(), buf, body);
    ASSERT_TRUE(written.has_value());

    auto view = fed::deserialize_untrusted_federation_entry(std::span<const std::uint8_t>(buf.data(), *written),
                                                            static_cast<std::uint16_t>(eff::OsUniverse::cardinality));
    ASSERT_TRUE(view.has_value());

    const auto expected_key = fed::federation_key<&t_unary, R0, int>();
    assert(view->header.content_hash == expected_key.content_hash);
    assert(view->header.row_hash == expected_key.row_hash);
    assert(view->payload.size() == body.size());
    for (std::size_t i = 0; i < body.size(); ++i) {
        assert(view->payload[i] == body[i]);
    }

    std::printf("  T10 codec_round_trip:                        PASSED\n");
}

// One function and one argument list across four rows. The streams differ
// because the row hash field differs, so a receiver cannot confuse two rows.
static void test_t11_cross_row_on_wire_distinct() {
    std::array<std::uint8_t, 32> buf_r0{};
    std::array<std::uint8_t, 32> buf_rbg{};
    std::array<std::uint8_t, 32> buf_rio{};
    std::array<std::uint8_t, 32> buf_rbgio{};

    auto w0 = fed::serialize_computation_cache_federation_entry<&t_unary, R0, int>(local_cipher_permission(), buf_r0,
                                                                                   std::span<const std::uint8_t>{});
    auto wb = fed::serialize_computation_cache_federation_entry<&t_unary, RBg, int>(local_cipher_permission(), buf_rbg,
                                                                                    std::span<const std::uint8_t>{});
    auto wi = fed::serialize_computation_cache_federation_entry<&t_unary, RIO, int>(local_cipher_permission(), buf_rio,
                                                                                    std::span<const std::uint8_t>{});
    auto wbi = fed::serialize_computation_cache_federation_entry<&t_unary, RBgIO, int>(
        local_cipher_permission(), buf_rbgio, std::span<const std::uint8_t>{});
    ASSERT_TRUE(w0.has_value());
    ASSERT_TRUE(wb.has_value());
    ASSERT_TRUE(wi.has_value());
    ASSERT_TRUE(wbi.has_value());

    auto bufs_differ = [](const std::array<std::uint8_t, 32>& a, const std::array<std::uint8_t, 32>& b) {
        for (std::size_t i = 0; i < 32; ++i) {
            if (a[i] != b[i]) return true;
        }
        return false;
    };
    assert(bufs_differ(buf_r0, buf_rbg));
    assert(bufs_differ(buf_r0, buf_rio));
    assert(bufs_differ(buf_r0, buf_rbgio));
    assert(bufs_differ(buf_rbg, buf_rio));
    assert(bufs_differ(buf_rbg, buf_rbgio));
    assert(bufs_differ(buf_rio, buf_rbgio));

    std::printf("  T11 cross_row_on_wire_distinct:              PASSED\n");
}

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

static void test_t13_composes_with_f11_cache_key() {
    constexpr auto fed_content = fed::federation_content_hash<&t_unary, RBg, int>().raw();
    constexpr auto f11_key = crucible::cipher::computation_cache_key_in_row<&t_unary, RBg, int>;
    static_assert(fed_content == f11_key, "The federation content hash equals the computation cache key for "
                                          "the same function, row and arguments. The projection between them "
                                          "is a plain wrap.");

    std::printf("  T13 composes_with_f11_cache_key:             PASSED\n");
}

static void test_t14_header_smoke_test() {
    bool ok = fed::computation_cache_federation_smoke_test();
    assert(ok);
    std::printf("  T14 header_smoke_test:                       PASSED\n");
}

// The decoded payload points into the caller's buffer just past the header
// rather than into a copy.
static void test_t15_payload_aliases_input() {
    const std::array<std::uint8_t, 8> body = {1, 2, 3, 4, 5, 6, 7, 8};
    std::array<std::uint8_t, 64> buf{};
    auto written =
        fed::serialize_computation_cache_federation_entry<&t_unary, R0, int>(local_cipher_permission(), buf, body);
    ASSERT_TRUE(written.has_value());

    auto view = fed::deserialize_untrusted_federation_entry(std::span<const std::uint8_t>(buf.data(), *written),
                                                            static_cast<std::uint16_t>(eff::OsUniverse::cardinality));
    ASSERT_TRUE(view.has_value());
    assert(view->payload.data() == buf.data() + fed::FEDERATION_HEADER_BYTES);

    std::printf("  T15 payload_aliases_input:                   PASSED\n");
}

// When the receiver is known to hold the bytes already, only the header need
// cross the wire: the two hashes identify the entry and the payload is
// elided. The content-addressed carrier stays the size of a span.
static void test_t16_content_addressed_payload_elision() {
    using Payload = fed::ComputationCacheFederationContentAddressedPayload<&t_unary, R0, int>;
    static_assert(crucible::safety::proto::is_content_addressed_v<typename Payload::payload_type>);
    static_assert(sizeof(Payload) == sizeof(std::span<const std::uint8_t>));

    const std::array<std::uint8_t, 16> body = {
        0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF,
    };

    std::array<std::uint8_t, 64> full_buf{};
    auto full_written = fed::serialize_computation_cache_federation_entry<&t_unary, R0, int>(local_cipher_permission(),
                                                                                             full_buf, Payload{body});
    ASSERT_TRUE(full_written.has_value());
    assert(*full_written == fed::FEDERATION_HEADER_BYTES + body.size());

    std::array<std::uint8_t, 64> hash_only_buf{};
    auto hash_only_written = fed::serialize_computation_cache_federation_entry<&t_unary, R0, int>(
        local_cipher_permission(), hash_only_buf, Payload::hash_only());
    ASSERT_TRUE(hash_only_written.has_value());
    assert(*hash_only_written == fed::FEDERATION_HEADER_BYTES);

    auto view = fed::deserialize_untrusted_federation_entry(
        std::span<const std::uint8_t>(hash_only_buf.data(), *hash_only_written),
        static_cast<std::uint16_t>(eff::OsUniverse::cardinality));
    ASSERT_TRUE(view.has_value());
    assert(view->payload.empty());
    assert((view->header.content_hash == fed::federation_key<&t_unary, R0, int>().content_hash));
    assert((view->header.row_hash == fed::federation_key<&t_unary, R0, int>().row_hash));

    std::printf("  T16 content_addressed_payload_elision:       PASSED\n");
}

// The tests above compare composite keys. Here only the content axis is read,
// and it has to separate rows on its own: the key fold mixes the row into the
// content hash rather than leaving the row axis to do all the work.
static void test_audit_a_content_axis_row_isolation() {
    static_assert(fed::federation_content_hash<&t_unary, R0, int>()
                      != fed::federation_content_hash<&t_unary, RBg, int>(),
                  "The content hash differs across rows even when read apart from the "
                  "row axis, because the key fold mixes the row into it.");
    static_assert(fed::federation_content_hash<&t_unary, RBg, int>()
                  != fed::federation_content_hash<&t_unary, RIO, int>());
    static_assert(fed::federation_content_hash<&t_unary, RBgIO, int>()
                  != fed::federation_content_hash<&t_unary, RFull, int>());

    // It still matches under a permutation of the row, since the fold sorts.
    static_assert(fed::federation_content_hash<&t_unary, RBgIO, int>()
                      == fed::federation_content_hash<&t_unary, RIOBg, int>(),
                  "The content hash inherits the row projection's permutation "
                  "invariance.");

    std::printf("  AUDIT-A content_axis_row_isolation:          PASSED\n");
}

// The header is 32 bytes, little-endian, laid out as:
//   0..3   magic, 'CFED'
//   4..5   protocol version
//   6..7   universe cardinality
//   8..15  content hash
//   16..23 row hash
//   24..27 payload size
//   28..31 reserved, zero
//
// Other implementations read those offsets, so a field that moves breaks
// compatibility silently. The reads below pin the three that matter.
static void test_audit_b_wire_byte_offset_stability() {
    constexpr auto k = fed::federation_key<&t_unary, RBgIO, int>();
    const auto expected_content = k.content_hash.raw();
    const auto expected_row = k.row_hash.raw();

    std::array<std::uint8_t, 64> buf{};
    auto written = fed::serialize_computation_cache_federation_entry<&t_unary, RBgIO, int>(
        local_cipher_permission(), buf, std::span<const std::uint8_t>{});
    ASSERT_TRUE(written.has_value());
    ASSERT_TRUE(*written == fed::FEDERATION_HEADER_BYTES);  // empty payload

    std::uint64_t observed_content = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        observed_content |= static_cast<std::uint64_t>(buf[8 + i]) << (i * 8);
    }
    assert(observed_content == expected_content);

    std::uint64_t observed_row = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        observed_row |= static_cast<std::uint64_t>(buf[16 + i]) << (i * 8);
    }
    assert(observed_row == expected_row);

    std::uint32_t observed_magic = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        observed_magic |= static_cast<std::uint32_t>(buf[i]) << (i * 8);
    }
    assert(observed_magic == fed::FEDERATION_MAGIC);

    std::printf("  AUDIT-B wire_byte_offset_stability:          PASSED\n");
}

// The effect universe only ever grows: atoms are appended, never removed or
// reordered. The header carries the writer's cardinality, and a receiver with
// a smaller one must refuse the entry, since it may name atoms that receiver
// has no definition for.
//
// The bridge does not stamp that field itself; the serialize path it calls
// does. What is under test is that the refusal still reaches a caller going
// through the bridge.
static void test_audit_c_cross_universe_cardinality_rejection() {
    constexpr auto current_cardinality = static_cast<std::uint16_t>(eff::OsUniverse::cardinality);
    static_assert(current_cardinality >= 1u, "The fixture subtracts one to build a smaller receiver cardinality, "
                                             "so the universe must hold at least one atom.");

    std::array<std::uint8_t, 64> buf{};
    const std::array<std::uint8_t, 4> body = {0xAA, 0xBB, 0xCC, 0xDD};

    auto written =
        fed::serialize_computation_cache_federation_entry<&t_unary, RFull, int>(local_cipher_permission(), buf, body);
    ASSERT_TRUE(written.has_value());

    // A receiver at the writer's cardinality accepts.
    {
        auto view = fed::deserialize_untrusted_federation_entry(std::span<const std::uint8_t>(buf.data(), *written),
                                                                current_cardinality);
        ASSERT_TRUE(view.has_value());
    }

    // One atom short of it refuses.
    {
        auto view = fed::deserialize_untrusted_federation_entry(std::span<const std::uint8_t>(buf.data(), *written),
                                                                static_cast<std::uint16_t>(current_cardinality - 1u));
        ASSERT_TRUE(!view.has_value());
        assert(view.error() == crucible::cipher::federation::FederationError::UniverseCardinalityTooHigh);
    }

    std::printf("  AUDIT-C cross_universe_cardinality_rejection: PASSED\n");
}

// Two permutations of one row produce equal keys. The serialized bytes have
// to match as well, or the codec would be sensitive to template argument
// order in some way the key projection does not expose.
static void test_audit_d_row_permutation_byte_invariance() {
    const std::array<std::uint8_t, 8> body = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80};

    std::array<std::uint8_t, 64> buf_bgio{};
    std::array<std::uint8_t, 64> buf_iobg{};

    auto wa = fed::serialize_computation_cache_federation_entry<&t_unary, RBgIO, int>(local_cipher_permission(),
                                                                                      buf_bgio, body);
    auto wb = fed::serialize_computation_cache_federation_entry<&t_unary, RIOBg, int>(local_cipher_permission(),
                                                                                      buf_iobg, body);
    ASSERT_TRUE(wa.has_value());
    ASSERT_TRUE(wb.has_value());
    assert(*wa == *wb);

    for (std::size_t i = 0; i < *wa; ++i) {
        assert(buf_bgio[i] == buf_iobg[i]);
    }

    std::printf("  AUDIT-D row_permutation_byte_invariance:     PASSED\n");
}

// The saturation case: a row naming every atom of the universe.
static void test_audit_e_saturation_row_round_trip() {
    constexpr auto k_full = fed::federation_key<&t_unary, RFull, int>();
    static_assert(!k_full.is_zero());
    static_assert(!k_full.is_sentinel());
    static_assert(k_full.row_hash.raw() != 0);

    static_assert(fed::federation_row_hash<RFull>() != fed::federation_row_hash<R0>());
    static_assert(fed::federation_row_hash<RFull>() != fed::federation_row_hash<RBg>());
    static_assert(fed::federation_row_hash<RFull>() != fed::federation_row_hash<RIO>());
    static_assert(fed::federation_row_hash<RFull>() != fed::federation_row_hash<RBgIO>());

    const std::array<std::uint8_t, 12> body = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE, 0x01, 0x02, 0x03, 0x04};
    std::array<std::uint8_t, 64> buf{};
    auto written =
        fed::serialize_computation_cache_federation_entry<&t_unary, RFull, int>(local_cipher_permission(), buf, body);
    ASSERT_TRUE(written.has_value());

    auto view = fed::deserialize_untrusted_federation_entry(std::span<const std::uint8_t>(buf.data(), *written),
                                                            static_cast<std::uint16_t>(eff::OsUniverse::cardinality));
    ASSERT_TRUE(view.has_value());
    assert(view->header.content_hash == k_full.content_hash);
    assert(view->header.row_hash == k_full.row_hash);
    assert(view->payload.size() == body.size());
    for (std::size_t i = 0; i < body.size(); ++i) {
        assert(view->payload[i] == body[i]);
    }

    std::printf("  AUDIT-E saturation_row_round_trip:           PASSED\n");
}

namespace {
namespace co = crucible::safety::diag::canonical_order;
namespace sf = crucible::safety;

// Stale sits outside Tagged in the canonical wrapper order.
using CanonStack = sf::Stale<sf::Tagged<int, sf::source::FromUser>>;
// This stack inverts that order.
using InvStack = sf::Tagged<sf::Stale<int>, sf::source::FromUser>;
}  // namespace

static void test_gate_a_canonical_args_accepted() {
    static_assert(fed::ArgsCanonicallyOrdered<int>);
    static_assert(fed::ArgsCanonicallyOrdered<int, double>);
    static_assert(fed::ArgsCanonicallyOrdered<CanonStack>);

    constexpr auto k = fed::federation_key<&t_unary, R0, CanonStack>();
    static_assert(!k.is_zero());
    static_assert(!k.is_sentinel());

    std::printf("  GATE-A canonical_args_accepted:              PASSED\n");
}

// The canonical-order predicate is consulted at the publish boundary, so an
// inverted stack cannot reach the wire. The compile failure itself is the
// negative fixture's job; these assertions witness the predicate.
static void test_gate_b_inverted_args_rejected() {
    static_assert(!co::CanonicallyOrdered<InvStack>, "Tagged outside Stale inverts the canonical wrapper order.");
    static_assert(!fed::ArgsCanonicallyOrdered<InvStack>,
                  "The federation argument gate rejects an inverted wrapper stack.");

    std::printf("  GATE-B inverted_args_rejected:               PASSED\n");
}

static void test_gate_c_canonical_stack_deterministic_distinct() {
    static_assert(fed::federation_key<&t_unary, R0, CanonStack>() == fed::federation_key<&t_unary, R0, CanonStack>());

    static_assert(fed::federation_key<&t_unary, R0, CanonStack>() != fed::federation_key<&t_unary, R0, int>(),
                  "A wrapped argument keys to its own cache slot and does not collapse "
                  "onto the bare payload.");

    std::printf("  GATE-C canonical_stack_deterministic_distinct: PASSED\n");
}

int main() {
    std::printf("test_computation_cache_federation — cache bridge\n");
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
    test_gate_a_canonical_args_accepted();
    test_gate_b_inverted_args_rejected();
    test_gate_c_canonical_stack_deterministic_distinct();
    std::printf("test_computation_cache_federation: 24 groups, all passed\n");
    return 0;
}
