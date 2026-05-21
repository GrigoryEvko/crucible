// ── test_fixy_substr_spsc_permissioned_spsc_channel — V-045 sentinel TU
//
// Forces evaluation of the header-internal static_asserts in
// `include/crucible/fixy/Substr.h` v045:: block under the project's
// warnings-as-errors flags (per
// feedback_header_only_static_assert_blind_spot.md), plus runtime
// witnesses on top of the compile-time identity sentinels.
//
// Covers the V-045 SPSC substrate-direct surface additions:
//   * PermissionedSpscChannel<T, Cap, UserTag>           — substrate alias
//   * SpscValue<T>                                       — concept re-export
//   * spsc_tag::{Whole,Producer,Consumer}<UserTag>       — tag tree
//   * SpscChannelSessionSurface<Channel>                 — surface concept
//   * mint_spsc_producer_endpoint(ch, perm)              — endpoint shim
//   * mint_spsc_consumer_endpoint(ch, perm)              — endpoint shim

#include <crucible/fixy/Substr.h>

#include <crucible/concurrent/PermissionedSpscChannel.h>
#include <crucible/permissions/Permission.h>

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <type_traits>
#include <utility>

namespace fsubstr = ::crucible::fixy::substr;
namespace cc      = ::crucible::concurrent;
namespace cs      = ::crucible::safety;

// ═══════════════════════════════════════════════════════════════════
// ── Synthetic UserTag for V-045 fixtures ─────────────────────────
// ═══════════════════════════════════════════════════════════════════

namespace probes {

// Use a TU-local tag so the splits_into_authoring_witness shipped by
// PermissionedSpscChannel.h's generic specialization picks up the
// fresh (Whole, Producer, Consumer) triple via the UserTag-parameterized
// templates.
struct V045TestUserTag {};

}  // namespace probes

using TestChannel =
    fsubstr::spsc::PermissionedSpscChannel<int, 32, probes::V045TestUserTag>;

// ═══════════════════════════════════════════════════════════════════
// ── Compile-time witnesses (re-state at TU scope) ────────────────
// ═══════════════════════════════════════════════════════════════════

// ── 1. Substrate alias identity ─────────────────────────────────
static_assert(std::is_same_v<
    TestChannel,
    cc::PermissionedSpscChannel<int, 32, probes::V045TestUserTag>>,
    "fixy::substr::spsc::PermissionedSpscChannel must alias the substrate.");

// ── 2. SpscValue concept re-export parity ────────────────────────
static_assert(fsubstr::spsc::SpscValue<int>);
static_assert(fsubstr::spsc::SpscValue<int> == cc::SpscValue<int>);
// Negative — a non-trivially-copyable type fails the concept on BOTH paths.
struct NonSpscValue { ~NonSpscValue() {} };  // non-trivial dtor
static_assert(!fsubstr::spsc::SpscValue<NonSpscValue>);
static_assert(!cc::SpscValue<NonSpscValue>);

// ── 3. Tag template identity — fixy path === concurrent path ─────
static_assert(std::is_same_v<
    fsubstr::spsc::spsc_tag::Whole<probes::V045TestUserTag>,
    cc::spsc_tag::Whole<probes::V045TestUserTag>>);
static_assert(std::is_same_v<
    fsubstr::spsc::spsc_tag::Producer<probes::V045TestUserTag>,
    cc::spsc_tag::Producer<probes::V045TestUserTag>>);
static_assert(std::is_same_v<
    fsubstr::spsc::spsc_tag::Consumer<probes::V045TestUserTag>,
    cc::spsc_tag::Consumer<probes::V045TestUserTag>>);

// ── 4. Member typedef parity through TestChannel ─────────────────
static_assert(std::is_same_v<typename TestChannel::value_type, int>);
static_assert(std::is_same_v<typename TestChannel::user_tag,
                             probes::V045TestUserTag>);
static_assert(std::is_same_v<typename TestChannel::whole_tag,
                             fsubstr::spsc::spsc_tag::Whole<probes::V045TestUserTag>>);
static_assert(std::is_same_v<typename TestChannel::producer_tag,
                             fsubstr::spsc::spsc_tag::Producer<probes::V045TestUserTag>>);
static_assert(std::is_same_v<typename TestChannel::consumer_tag,
                             fsubstr::spsc::spsc_tag::Consumer<probes::V045TestUserTag>>);

// ── 5. channel_capacity value parity ─────────────────────────────
static_assert(TestChannel::channel_capacity == 32);

// ── 6. SpscChannelSessionSurface admits the representative channel
static_assert(fsubstr::spsc::SpscChannelSessionSurface<TestChannel>);

// ═══════════════════════════════════════════════════════════════════
// ── Runtime witnesses ────────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════

// Construct + mint root permission via fixy::substr::spsc::spsc_tag::Whole.
// mint_permission_root<Tag>() (no-ctx form) is valid here because the SPSC
// tag tree's permission_row<Tag> is empty (channel handles are EmptyPermSet —
// no wire-permission transfer).
static void test_runtime_construct_and_split() {
    TestChannel ch{};
    auto whole = cs::mint_permission_root<TestChannel::whole_tag>();
    auto [prod_perm, cons_perm] = cs::mint_permission_split<
        TestChannel::producer_tag,
        TestChannel::consumer_tag>(std::move(whole));
    auto producer = fsubstr::spsc::mint_spsc_producer_endpoint(
        ch, std::move(prod_perm));
    auto consumer = fsubstr::spsc::mint_spsc_consumer_endpoint(
        ch, std::move(cons_perm));
    (void)producer;
    (void)consumer;
}

// Producer/consumer endpoint round-trip — push N, pop N, verify FIFO order.
static void test_runtime_push_pop_roundtrip() {
    TestChannel ch{};
    auto whole = cs::mint_permission_root<TestChannel::whole_tag>();
    auto [prod_perm, cons_perm] = cs::mint_permission_split<
        TestChannel::producer_tag,
        TestChannel::consumer_tag>(std::move(whole));
    auto producer = fsubstr::spsc::mint_spsc_producer_endpoint(
        ch, std::move(prod_perm));
    auto consumer = fsubstr::spsc::mint_spsc_consumer_endpoint(
        ch, std::move(cons_perm));

    constexpr int N = 8;  // safely under capacity (32)
    for (int i = 0; i < N; ++i) {
        if (!producer.try_push(i * 100 + 7)) std::abort();
    }
    for (int i = 0; i < N; ++i) {
        std::optional<int> r = consumer.try_pop();
        if (!r) std::abort();
        if (*r != i * 100 + 7) std::abort();
    }
    // Drained.
    if (consumer.try_pop()) std::abort();
}

// empty_approx / size_approx telemetry pass-through.
static void test_runtime_telemetry_passes_through() {
    TestChannel ch{};
    auto whole = cs::mint_permission_root<TestChannel::whole_tag>();
    auto [prod_perm, cons_perm] = cs::mint_permission_split<
        TestChannel::producer_tag,
        TestChannel::consumer_tag>(std::move(whole));
    auto producer = fsubstr::spsc::mint_spsc_producer_endpoint(
        ch, std::move(prod_perm));
    auto consumer = fsubstr::spsc::mint_spsc_consumer_endpoint(
        ch, std::move(cons_perm));

    if (!producer.empty_approx()) std::abort();
    if (producer.size_approx() != 0) std::abort();

    for (int i = 0; i < 4; ++i) {
        if (!producer.try_push(i)) std::abort();
    }
    if (producer.empty_approx()) std::abort();
    if (producer.size_approx() != 4) std::abort();

    // capacity() readback on both sides.
    if (producer.capacity() != 32) std::abort();
    if (consumer.capacity() != 32) std::abort();
}

// Capacity-bound: pushing past capacity returns false; popping all
// followed by another pop returns nullopt.
static void test_runtime_capacity_bound() {
    TestChannel ch{};
    auto whole = cs::mint_permission_root<TestChannel::whole_tag>();
    auto [prod_perm, cons_perm] = cs::mint_permission_split<
        TestChannel::producer_tag,
        TestChannel::consumer_tag>(std::move(whole));
    auto producer = fsubstr::spsc::mint_spsc_producer_endpoint(
        ch, std::move(prod_perm));
    auto consumer = fsubstr::spsc::mint_spsc_consumer_endpoint(
        ch, std::move(cons_perm));

    // Fill to capacity.
    for (std::size_t i = 0; i < TestChannel::channel_capacity; ++i) {
        if (!producer.try_push(static_cast<int>(i))) std::abort();
    }
    // One more must fail.
    if (producer.try_push(999)) std::abort();
    // Drain.
    for (std::size_t i = 0; i < TestChannel::channel_capacity; ++i) {
        std::optional<int> r = consumer.try_pop();
        if (!r) std::abort();
        if (*r != static_cast<int>(i)) std::abort();
    }
    // Drained → nullopt.
    if (consumer.try_pop()) std::abort();
}

// with_recombined_access recombines producer+consumer halves back to
// Whole and runs a body on the channel.  Same surface as the substrate.
// Body is invoked with no args (the channel is captured by the caller).
static void test_runtime_recombined_access() {
    TestChannel ch{};
    auto whole = cs::mint_permission_root<TestChannel::whole_tag>();
    int sentinel = 0;
    auto recombined = ch.with_recombined_access(
        std::move(whole),
        [&sentinel]() {
            // Whole-scope body — sentinel proves the body actually ran.
            sentinel = 42;
        });
    (void)recombined;
    if (sentinel != 42) std::abort();
}

// Substrate-pointer identity — endpoint mints return EXACTLY the
// substrate's ProducerHandle / ConsumerHandle, not a fixy-side wrapper.
static void test_runtime_endpoint_handle_identity() {
    TestChannel ch{};
    auto whole = cs::mint_permission_root<TestChannel::whole_tag>();
    auto [prod_perm, cons_perm] = cs::mint_permission_split<
        TestChannel::producer_tag,
        TestChannel::consumer_tag>(std::move(whole));
    auto producer = fsubstr::spsc::mint_spsc_producer_endpoint(
        ch, std::move(prod_perm));
    auto consumer = fsubstr::spsc::mint_spsc_consumer_endpoint(
        ch, std::move(cons_perm));
    static_assert(std::is_same_v<decltype(producer),
                                 typename TestChannel::ProducerHandle>);
    static_assert(std::is_same_v<decltype(consumer),
                                 typename TestChannel::ConsumerHandle>);
}

// ProtocolType aliases unchanged from pre-V-045 surface — re-verify
// at runtime scope (compile-time witness lives in Substr.h's U-103
// block already; this is a runtime-callsite parity rail).
static void test_runtime_protocol_aliases_unchanged() {
    using FixyProd = fsubstr::spsc::ProducerProto<int>;
    using FixyCons = fsubstr::spsc::ConsumerProto<int>;
    using SubsProd = ::crucible::safety::proto::spsc_session::ProducerProto<int>;
    using SubsCons = ::crucible::safety::proto::spsc_session::ConsumerProto<int>;
    static_assert(std::is_same_v<FixyProd, SubsProd>);
    static_assert(std::is_same_v<FixyCons, SubsCons>);
}

// ═══════════════════════════════════════════════════════════════════
// ── Driver ────────────────────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════

int main() {
    test_runtime_construct_and_split();
    test_runtime_push_pop_roundtrip();
    test_runtime_telemetry_passes_through();
    test_runtime_capacity_bound();
    test_runtime_recombined_access();
    test_runtime_endpoint_handle_identity();
    test_runtime_protocol_aliases_unchanged();
    std::printf("test_fixy_substr_spsc_permissioned_spsc_channel: "
                "7/7 runtime witnesses passed\n");
    return 0;
}
