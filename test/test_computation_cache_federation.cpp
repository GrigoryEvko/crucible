#include <crucible/cipher/ComputationCacheFederation.h>
#include <crucible/cipher/ComputationCache.h>
#include <crucible/cipher/FederationProtocol.h>
#include <crucible/Types.h>
#include <fixy/CanonicalOrder.h>
#include <fixy/Federation.h>
#include <fixy/Stale.h>
#include <fixy/Tagged.h>
#include <foundation/core/StableIdentity.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>
#include <foundation/permissions/Permission.h>

#include "test_assert.h"

#include <cstddef>
#include <cstdint>

using namespace crucible;
namespace fed = crucible::cipher::federation;
namespace eff = ::foundation::effects;

#define ASSERT_TRUE(...) assert((__VA_ARGS__))

// The value of a result that holds one.  A result with an error ends the
// test.
template <class T, class E>
[[nodiscard]] static T value_of(::fixy::Result<T, E> const& result) {
    return result.match([](T const& value) { return value; },
                        [](E const&) -> T { ::fixy::fatal("the result holds an error"); });
}

// A buffer of N bytes.
template <std::size_t N>
using Bytes = ::foundation::core::FixedArray<std::byte, N>;

inline constexpr std::size_t kHeader = fed::FEDERATION_HEADER_BYTES;

// The bytes of the integer values, in order.
template <class... Values>
[[nodiscard]] static constexpr Bytes<sizeof...(Values)> bytes_of(Values... values) {
    return Bytes<sizeof...(Values)>{static_cast<std::byte>(values)...};
}

// The first count bytes of a buffer: the bytes of an entry that a
// serialize wrote.
template <std::size_t N>
[[nodiscard]] static ::fixy::View<std::byte const> prefix(Bytes<N> const& buf, std::size_t count) {
    return ::fixy::View<std::byte const>{buf.view()}.window(0, count).expect("the count is inside the buffer");
}

// The key of a federation of the tests: the bytes first, first + 1, and so
// on.
[[nodiscard]] static constexpr fed::FederationHashKey hash_key_from(std::uint8_t first) noexcept {
    Bytes<32> bytes{};
    std::uint8_t next = first;
    for (std::byte& byte : bytes) {
        byte = static_cast<std::byte>(next);
        ++next;
    }
    return fed::FederationHashKey{::fixy::View<std::byte const, 32>{bytes.view()}};
}

[[nodiscard]] static constexpr fed::FederationHashKey test_hash_key() noexcept { return hash_key_from(0); }

// These fixtures carry their own names rather than the ones the header's
// self-test uses, so a translation unit that runs both has no collision.
// They stand outside the unnamed namespace, because a key refuses a
// function with internal linkage.

namespace test_computation_cache_federation_functions {
inline void t_unary(int) noexcept {}
inline void t_binary(int, double) noexcept {}
inline void t_void() noexcept {}
inline void t_other(int) noexcept {}  // same signature as t_unary
}  // namespace test_computation_cache_federation_functions

namespace {
using namespace test_computation_cache_federation_functions;

using R0 = eff::Row<>;
using RBg = eff::Row<eff::Effect::Bg>;
using RIO = eff::Row<eff::Effect::IO>;
using RBgIO = eff::Row<eff::Effect::Bg, eff::Effect::IO>;
using RIOBg = eff::Row<eff::Effect::IO, eff::Effect::Bg>;
using RIOBgIO = eff::Row<eff::Effect::IO, eff::Effect::Bg, eff::Effect::IO>;
using RFull = eff::Row<eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block, eff::Effect::Bg, eff::Effect::Init,
                       eff::Effect::Test>;

// The local cipher permission declares an IO row, so its root is minted
// under a context that admits IO.
using IoCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::IO>>;

const auto& local_cipher_permission() {
    static const auto permission =
        ::foundation::permissions::mint_permission_root<::fixy::federation::LocalCipherTag>(IoCtx{eff::testing::bg()});
    return permission;
}

// True when the text appears in the bytes.  Complexity: the product of
// the two lengths.
[[nodiscard]] bool holds_text(::fixy::View<std::byte const> bytes, ::fixy::TextView text) {
    if (text.size() > bytes.size()) return false;
    for (std::size_t start = 0; start + text.size() <= bytes.size(); ++start) {
        bool is_match = true;
        std::size_t at = start;
        for (char const letter : text) {
            if (*bytes.get(at).expect("the window is inside the bytes") != static_cast<std::byte>(letter)) {
                is_match = false;
                break;
            }
            ++at;
        }
        if (is_match) return true;
    }
    return false;
}
}  // namespace

// The identity of a computation holds the stable name of the function, of
// the mark of the computation keys, of the canonical row and of each
// argument type.
static void test_t01_identity_holds_the_names() {
    const ::fixy::View<std::byte const> identity = fed::computation_identity<&t_binary, RBgIO, int, double>();
    assert(holds_text(identity, ::foundation::core::stable_function_name_text<&t_binary>()));
    assert(holds_text(identity, ::foundation::core::stable_name_text<fed::ComputationCacheIdentity>()));
    assert(holds_text(identity, ::foundation::core::stable_name_text<eff::canonical_row_t<RBgIO>>()));
    assert(holds_text(identity, ::foundation::core::stable_name_text<int>()));
    assert(holds_text(identity, ::foundation::core::stable_name_text<double>()));

    crucible::test::pass("  T01 identity_holds_the_names:                PASSED\n");
}

// The key is the keyed hash of the identity under the key of the
// federation, so it reads the names and no 64-bit id.
static void test_t02_key_is_the_hash_of_the_identity() {
    const fed::FederationKey key = fed::federation_key<&t_unary, RBg, int>(test_hash_key());
    assert(key == fed::federation_key_of_bytes(test_hash_key(), fed::computation_identity<&t_unary, RBg, int>()));
    assert(key.kind() == fed::FederationHashKind::blake3_keyed);
    assert(key.width() == fed::FederationKeyWidth::bytes_32);

    crucible::test::pass("  T02 key_is_the_hash_of_the_identity:         PASSED\n");
}

// The key of one computation in two federations differs, so an entry of
// one federation does not name a slot of another.
static void test_t03_federation_key_takes_part() {
    assert(!(fed::federation_key<&t_unary, R0, int>(hash_key_from(0))
             == fed::federation_key<&t_unary, R0, int>(hash_key_from(1))));

    crucible::test::pass("  T03 federation_key_takes_part:               PASSED\n");
}

static void test_t04_deterministic() {
    static_assert(fed::federation_key<&t_unary, R0, int>(test_hash_key())
                  == fed::federation_key<&t_unary, R0, int>(test_hash_key()));
    ASSERT_TRUE(fed::federation_key<&t_binary, RBgIO, int, double>(test_hash_key())
                == fed::federation_key<&t_binary, RBgIO, int, double>(test_hash_key()));

    crucible::test::pass("  T04 deterministic:                           PASSED\n");
}

static void test_t05_row_distinguishes() {
    const auto key = test_hash_key();
    assert(!(fed::federation_key<&t_unary, R0, int>(key) == fed::federation_key<&t_unary, RBg, int>(key)));
    assert(!(fed::federation_key<&t_unary, R0, int>(key) == fed::federation_key<&t_unary, RIO, int>(key)));
    assert(!(fed::federation_key<&t_unary, RBg, int>(key) == fed::federation_key<&t_unary, RIO, int>(key)));
    assert(!(fed::federation_key<&t_unary, RBg, int>(key) == fed::federation_key<&t_unary, RBgIO, int>(key)));
    assert(!(fed::federation_key<&t_unary, RBgIO, int>(key) == fed::federation_key<&t_unary, RFull, int>(key)));

    crucible::test::pass("  T05 row_distinguishes:                       PASSED\n");
}

// Two functions of one signature share their 64-bit function id, and
// their names keep them apart.
static void test_t06_fnptr_distinguishes() {
    const auto key = test_hash_key();
    assert(!(fed::federation_key<&t_unary, R0, int>(key) == fed::federation_key<&t_other, R0, int>(key)));
    assert(!(fed::federation_key<&t_unary, R0, int>(key) == fed::federation_key<&t_void, R0>(key)));

    crucible::test::pass("  T06 fnptr_distinguishes:                     PASSED\n");
}

static void test_t07_args_distinguishes() {
    const auto key = test_hash_key();
    assert(!(fed::federation_key<&t_unary, R0, int>(key) == fed::federation_key<&t_unary, R0, float>(key)));
    assert(!(fed::federation_key<&t_binary, R0, int, double>(key)
             == fed::federation_key<&t_binary, R0, double, int>(key)));

    crucible::test::pass("  T07 args_distinguishes:                      PASSED\n");
}

// A row is a set of atoms: its order and a repeated atom do not change the
// key.
static void test_t08_row_permutation_invariance() {
    const auto key = test_hash_key();
    ASSERT_TRUE(fed::federation_key<&t_unary, RBgIO, int>(key) == fed::federation_key<&t_unary, RIOBg, int>(key));
    ASSERT_TRUE(fed::federation_key<&t_unary, RBgIO, int>(key) == fed::federation_key<&t_unary, RIOBgIO, int>(key));

    crucible::test::pass("  T08 row_permutation_invariance:              PASSED\n");
}

static void test_t09_empty_args() {
    const auto key = test_hash_key();
    const fed::FederationKey k_void_empty = fed::federation_key<&t_void, R0>(key);
    const fed::FederationKey k_void_bg = fed::federation_key<&t_void, RBg>(key);
    assert(!k_void_empty.is_zero());
    assert(!(k_void_empty == k_void_bg));

    crucible::test::pass("  T09 empty_args:                              PASSED\n");
}

static void test_t10_codec_round_trip() {
    const auto body =
        bytes_of(0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10);

    Bytes<kHeader + 16> buf{};
    auto written = fed::serialize_computation_cache_federation_entry<&t_unary, R0, int>(
        local_cipher_permission(), test_hash_key(), buf.view(), body.view());
    ASSERT_TRUE(written.is_ok());

    auto view = fed::deserialize_untrusted_federation_entry(prefix(buf, value_of(written)),
                                                            fed::FEDERATION_UNIVERSE_CARDINALITY);
    ASSERT_TRUE(view.is_ok());

    ASSERT_TRUE(value_of(view).header.key == fed::federation_key<&t_unary, R0, int>(test_hash_key()));
    assert(::fixy::equal(value_of(view).payload, body.view()));

    crucible::test::pass("  T10 codec_round_trip:                        PASSED\n");
}

// One function and one argument list across four rows. The streams differ
// because the key differs, so a receiver cannot confuse two rows.
static void test_t11_cross_row_on_wire_distinct() {
    Bytes<kHeader> buf_r0{};
    Bytes<kHeader> buf_rbg{};
    Bytes<kHeader> buf_rio{};
    Bytes<kHeader> buf_rbgio{};

    auto w0 = fed::serialize_computation_cache_federation_entry<&t_unary, R0, int>(
        local_cipher_permission(), test_hash_key(), buf_r0.view(), ::fixy::View<std::byte const>{});
    auto wb = fed::serialize_computation_cache_federation_entry<&t_unary, RBg, int>(
        local_cipher_permission(), test_hash_key(), buf_rbg.view(), ::fixy::View<std::byte const>{});
    auto wi = fed::serialize_computation_cache_federation_entry<&t_unary, RIO, int>(
        local_cipher_permission(), test_hash_key(), buf_rio.view(), ::fixy::View<std::byte const>{});
    auto wbi = fed::serialize_computation_cache_federation_entry<&t_unary, RBgIO, int>(
        local_cipher_permission(), test_hash_key(), buf_rbgio.view(), ::fixy::View<std::byte const>{});
    ASSERT_TRUE(w0.is_ok());
    ASSERT_TRUE(wb.is_ok());
    ASSERT_TRUE(wi.is_ok());
    ASSERT_TRUE(wbi.is_ok());

    // Each entry fills its buffer of 48 bytes.
    assert(!::fixy::equal(buf_r0.view(), buf_rbg.view()));
    assert(!::fixy::equal(buf_r0.view(), buf_rio.view()));
    assert(!::fixy::equal(buf_r0.view(), buf_rbgio.view()));
    assert(!::fixy::equal(buf_rbg.view(), buf_rio.view()));
    assert(!::fixy::equal(buf_rbg.view(), buf_rbgio.view()));
    assert(!::fixy::equal(buf_rio.view(), buf_rbgio.view()));

    crucible::test::pass("  T11 cross_row_on_wire_distinct:              PASSED\n");
}

static void test_t12_key_never_sentinel_or_zero() {
    const auto key = test_hash_key();
    const fed::FederationKey k0 = fed::federation_key<&t_unary, R0, int>(key);
    const fed::FederationKey kb = fed::federation_key<&t_unary, RBg, int>(key);
    const fed::FederationKey ki = fed::federation_key<&t_unary, RIO, int>(key);
    const fed::FederationKey kf = fed::federation_key<&t_unary, RFull, int>(key);

    assert(!k0.is_zero() && !k0.is_sentinel());
    assert(!kb.is_zero() && !kb.is_sentinel());
    assert(!ki.is_zero() && !ki.is_sentinel());
    assert(!kf.is_zero() && !kf.is_sentinel());

    crucible::test::pass("  T12 key_never_sentinel_or_zero:              PASSED\n");
}

static void test_t14_header_smoke_test() {
    const bool is_smoke_green =
        fed::computation_cache_federation_smoke_test(local_cipher_permission(), test_hash_key());
    assert(is_smoke_green);
    crucible::test::pass("  T14 header_smoke_test:                       PASSED\n");
}

// The decoded payload points into the caller's buffer just past the header
// rather than into a copy.
static void test_t15_payload_aliases_input() {
    const auto body = bytes_of(1, 2, 3, 4, 5, 6, 7, 8);
    Bytes<kHeader + 16> buf{};
    auto written = fed::serialize_computation_cache_federation_entry<&t_unary, R0, int>(
        local_cipher_permission(), test_hash_key(), buf.view(), body.view());
    ASSERT_TRUE(written.is_ok());

    auto view = fed::deserialize_untrusted_federation_entry(prefix(buf, value_of(written)),
                                                            fed::FEDERATION_UNIVERSE_CARDINALITY);
    ASSERT_TRUE(view.is_ok());
    // The first byte of the payload is the byte of the buffer after the header.
    assert(::fixy::same_object(value_of(view).payload.get(0).expect("the payload holds eight bytes"),
                               buf.get(fed::FEDERATION_HEADER_BYTES).expect("the buffer holds the payload")));

    crucible::test::pass("  T15 payload_aliases_input:                   PASSED\n");
}

// When the receiver is known to hold the bytes already, only the header need
// cross the wire: the key identifies the entry and the payload is elided.
// The content-addressed carrier stays the size of a span.
static void test_t16_content_addressed_payload_elision() {
    using Payload = fed::ComputationCacheFederationContentAddressedPayload<&t_unary, R0, int>;
    static_assert(::fixy::session::is_content_addressed_v<typename Payload::payload_type>);
    static_assert(sizeof(Payload) == sizeof(::fixy::View<std::byte const>));

    const auto body =
        bytes_of(0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF);

    Bytes<kHeader + 16> full_buf{};
    auto full_written = fed::serialize_computation_cache_federation_entry<&t_unary, R0, int>(
        local_cipher_permission(), test_hash_key(), full_buf.view(), Payload{body.view()});
    ASSERT_TRUE(full_written.is_ok());
    assert(value_of(full_written) == fed::FEDERATION_HEADER_BYTES + body.size());

    Bytes<kHeader + 16> hash_only_buf{};
    auto hash_only_written = fed::serialize_computation_cache_federation_entry<&t_unary, R0, int>(
        local_cipher_permission(), test_hash_key(), hash_only_buf.view(), Payload::hash_only());
    ASSERT_TRUE(hash_only_written.is_ok());
    assert(value_of(hash_only_written) == fed::FEDERATION_HEADER_BYTES);

    auto view = fed::deserialize_untrusted_federation_entry(prefix(hash_only_buf, value_of(hash_only_written)),
                                                            fed::FEDERATION_UNIVERSE_CARDINALITY);
    ASSERT_TRUE(view.is_ok());
    assert(value_of(view).payload.size() == 0);
    ASSERT_TRUE(value_of(view).header.key == fed::federation_key<&t_unary, R0, int>(test_hash_key()));

    crucible::test::pass("  T16 content_addressed_payload_elision:       PASSED\n");
}

// Other implementations read the offsets of crucible/cipher/
// FederationProtocol.h, so a field that moves breaks compatibility
// silently.  The reads below pin the magic, the kind, the width and the
// digest of the key.
static void test_wire_byte_offset_stability() {
    const fed::FederationKey key = fed::federation_key<&t_unary, RBgIO, int>(test_hash_key());

    Bytes<kHeader> buf{};
    auto written = fed::serialize_computation_cache_federation_entry<&t_unary, RBgIO, int>(
        local_cipher_permission(), test_hash_key(), buf.view(), ::fixy::View<std::byte const>{});
    ASSERT_TRUE(written.is_ok());
    ASSERT_TRUE(value_of(written) == fed::FEDERATION_HEADER_BYTES);  // empty payload

    assert(buf.at(::foundation::core::index<0>()) == std::byte{'C'});
    assert(buf.at(::foundation::core::index<12>()) == std::byte{1});
    assert(buf.at(::foundation::core::index<13>()) == std::byte{32});
    assert(::fixy::equal(prefix(buf, kHeader).window(14, 32).expect("the header holds the digest"),
                         ::fixy::View<std::byte const>{key.digest()}));

    crucible::test::pass("  wire_byte_offset_stability:                  PASSED\n");
}

// The effect universe only ever grows: atoms are appended, never removed or
// reordered. The header carries the writer's cardinality, and a receiver with
// a smaller one must refuse the entry, since it may name atoms that receiver
// has no definition for.
//
// The bridge does not stamp that field itself; the serialize path it calls
// does. What is under test is that the refusal still reaches a caller going
// through the bridge.
static void test_cross_universe_cardinality_rejection() {
    constexpr auto current_cardinality = static_cast<std::uint16_t>(::foundation::effects::effect_count);
    static_assert(current_cardinality >= 1u, "The fixture subtracts one to build a smaller receiver cardinality, "
                                             "so the universe must hold at least one atom.");

    Bytes<kHeader + 16> buf{};
    const auto body = bytes_of(0xAA, 0xBB, 0xCC, 0xDD);

    auto written = fed::serialize_computation_cache_federation_entry<&t_unary, RFull, int>(
        local_cipher_permission(), test_hash_key(), buf.view(), body.view());
    ASSERT_TRUE(written.is_ok());

    // A receiver at the writer's cardinality accepts.
    {
        auto view = fed::deserialize_untrusted_federation_entry(prefix(buf, value_of(written)), current_cardinality);
        ASSERT_TRUE(view.is_ok());
    }

    // One atom short of it refuses.
    {
        auto view = fed::deserialize_untrusted_federation_entry(prefix(buf, value_of(written)),
                                                                static_cast<std::uint16_t>(current_cardinality - 1u));
        ASSERT_TRUE(!view.is_ok());
        assert(view.err().expect("the view holds an error")
               == crucible::cipher::federation::FederationError::UniverseCardinalityTooHigh);
    }

    crucible::test::pass("  cross_universe_cardinality_rejection:        PASSED\n");
}

// Two permutations of one row produce equal keys. The serialized bytes have
// to match as well, or the codec would be sensitive to template argument
// order in some way the key projection does not expose.
static void test_row_permutation_byte_invariance() {
    const auto body = bytes_of(0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80);

    Bytes<kHeader + 16> buf_bgio{};
    Bytes<kHeader + 16> buf_iobg{};

    auto wa = fed::serialize_computation_cache_federation_entry<&t_unary, RBgIO, int>(
        local_cipher_permission(), test_hash_key(), buf_bgio.view(), body.view());
    auto wb = fed::serialize_computation_cache_federation_entry<&t_unary, RIOBg, int>(
        local_cipher_permission(), test_hash_key(), buf_iobg.view(), body.view());
    ASSERT_TRUE(wa.is_ok());
    ASSERT_TRUE(wb.is_ok());
    assert(value_of(wa) == value_of(wb));

    assert(::fixy::equal(prefix(buf_bgio, value_of(wa)), prefix(buf_iobg, value_of(wb))));

    crucible::test::pass("  row_permutation_byte_invariance:             PASSED\n");
}

// The saturation case: a row naming every atom of the universe.
static void test_saturation_row_round_trip() {
    const fed::FederationKey k_full = fed::federation_key<&t_unary, RFull, int>(test_hash_key());
    assert(!k_full.is_zero());
    assert(!k_full.is_sentinel());

    const auto body = bytes_of(0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE, 0x01, 0x02, 0x03, 0x04);
    Bytes<kHeader + 16> buf{};
    auto written = fed::serialize_computation_cache_federation_entry<&t_unary, RFull, int>(
        local_cipher_permission(), test_hash_key(), buf.view(), body.view());
    ASSERT_TRUE(written.is_ok());

    auto view = fed::deserialize_untrusted_federation_entry(prefix(buf, value_of(written)),
                                                            fed::FEDERATION_UNIVERSE_CARDINALITY);
    ASSERT_TRUE(view.is_ok());
    assert(value_of(view).header.key == k_full);
    assert(::fixy::equal(value_of(view).payload, body.view()));

    crucible::test::pass("  saturation_row_round_trip:                   PASSED\n");
}

namespace {
namespace co = ::fixy::canonical_order;
using FromUser = ::fixy::tags::source::FromUser;

// Stale sits outside Tagged in the canonical wrapper order.
using CanonStack = ::fixy::Stale<::fixy::Tagged<int, FromUser>>;
// This stack inverts that order.
using InvStack = ::fixy::Tagged<::fixy::Stale<int>, FromUser>;
}  // namespace

static void test_gate_a_canonical_args_accepted() {
    static_assert(fed::ArgsCanonicallyOrdered<int>);
    static_assert(fed::ArgsCanonicallyOrdered<int, double>);
    static_assert(fed::ArgsCanonicallyOrdered<CanonStack>);

    const fed::FederationKey k = fed::federation_key<&t_unary, R0, CanonStack>(test_hash_key());
    assert(!k.is_zero());
    assert(!k.is_sentinel());

    crucible::test::pass("  GATE-A canonical_args_accepted:              PASSED\n");
}

// The canonical-order predicate is consulted at the publish boundary, so an
// inverted stack cannot reach the wire. The compile failure itself is the
// negative fixture's job; these assertions witness the predicate.
static void test_gate_b_inverted_args_rejected() {
    static_assert(!co::CanonicallyOrdered<InvStack>, "Tagged outside Stale inverts the canonical wrapper order.");
    static_assert(!fed::ArgsCanonicallyOrdered<InvStack>,
                  "The federation argument gate rejects an inverted wrapper stack.");

    crucible::test::pass("  GATE-B inverted_args_rejected:               PASSED\n");
}

static void test_gate_c_canonical_stack_deterministic_distinct() {
    const auto key = test_hash_key();
    ASSERT_TRUE(fed::federation_key<&t_unary, R0, CanonStack>(key)
                == fed::federation_key<&t_unary, R0, CanonStack>(key));

    // A wrapped argument keys to its own cache slot and does not collapse
    // onto the bare payload.
    assert(!(fed::federation_key<&t_unary, R0, CanonStack>(key) == fed::federation_key<&t_unary, R0, int>(key)));

    crucible::test::pass("  GATE-C canonical_stack_deterministic_distinct: PASSED\n");
}

int main() {
    ::fixy::report(::fixy::Sink::Out, "test_computation_cache_federation — cache bridge\n");
    test_t01_identity_holds_the_names();
    test_t02_key_is_the_hash_of_the_identity();
    test_t03_federation_key_takes_part();
    test_t04_deterministic();
    test_t05_row_distinguishes();
    test_t06_fnptr_distinguishes();
    test_t07_args_distinguishes();
    test_t08_row_permutation_invariance();
    test_t09_empty_args();
    test_t10_codec_round_trip();
    test_t11_cross_row_on_wire_distinct();
    test_t12_key_never_sentinel_or_zero();
    test_t14_header_smoke_test();
    test_t15_payload_aliases_input();
    test_t16_content_addressed_payload_elision();
    test_wire_byte_offset_stability();
    test_cross_universe_cardinality_rejection();
    test_row_permutation_byte_invariance();
    test_saturation_row_round_trip();
    test_gate_a_canonical_args_accepted();
    test_gate_b_inverted_args_rejected();
    test_gate_c_canonical_stack_deterministic_distinct();
    crucible::test::pass("test_computation_cache_federation: 22 groups, all passed\n");
    return 0;
}
