// ── test_fixy_substr_mpmc_permissioned_mpmc_channel — V-046 sentinel TU
//
// Forces evaluation of the header-internal static_asserts in
// `include/crucible/fixy/Substr.h` v046:: block under the project's
// warnings-as-errors flags (per
// feedback_header_only_static_assert_blind_spot.md), plus runtime
// witnesses on top of the compile-time identity sentinels.
//
// Covers the V-046 MPMC substrate-direct surface additions:
//   * PermissionedMpmcChannel<T, Cap, UserTag>            — substrate alias
//   * MpmcValue<T>                                        — concept re-export
//   * mpmc_tag::{Whole,Producer,Consumer}<UserTag>        — tag tree
//   (MpmcChannelSessionSurface already shipped pre-V-046)
//   (mint_mpmc_*_endpoint already shipped pre-V-046 via using-decl)

#include <crucible/fixy/Substr.h>

#include <crucible/concurrent/PermissionedMpmcChannel.h>
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
// ── Synthetic UserTag for V-046 fixtures ─────────────────────────
// ═══════════════════════════════════════════════════════════════════

namespace probes {

// TU-local tag so PermissionedMpmcChannel.h's generic specialization
// picks up the fresh (Whole, Producer, Consumer) triple via the
// UserTag-parameterized templates.
struct V046TestUserTag {};

}  // namespace probes

using TestChannel =
    fsubstr::mpmc::PermissionedMpmcChannel<int, 64, probes::V046TestUserTag>;

// ═══════════════════════════════════════════════════════════════════
// ── Compile-time witnesses (re-state at TU scope) ────────────────
// ═══════════════════════════════════════════════════════════════════

// ── 1. Substrate alias identity ─────────────────────────────────
static_assert(std::is_same_v<
    TestChannel,
    cc::PermissionedMpmcChannel<int, 64, probes::V046TestUserTag>>,
    "fixy::substr::mpmc::PermissionedMpmcChannel must alias the substrate.");

// ── 2. MpmcValue concept re-export parity ────────────────────────
static_assert(fsubstr::mpmc::MpmcValue<int>);
static_assert(fsubstr::mpmc::MpmcValue<int> == cc::MpmcValue<int>);
// Negative — a non-trivially-copyable type fails the concept on BOTH paths.
struct NonMpmcValue { ~NonMpmcValue() {} };  // non-trivial dtor
static_assert(!fsubstr::mpmc::MpmcValue<NonMpmcValue>);
static_assert(!cc::MpmcValue<NonMpmcValue>);

// ── 3. Tag template identity — fixy path === concurrent path ─────
static_assert(std::is_same_v<
    fsubstr::mpmc::mpmc_tag::Whole<probes::V046TestUserTag>,
    cc::mpmc_tag::Whole<probes::V046TestUserTag>>);
static_assert(std::is_same_v<
    fsubstr::mpmc::mpmc_tag::Producer<probes::V046TestUserTag>,
    cc::mpmc_tag::Producer<probes::V046TestUserTag>>);
static_assert(std::is_same_v<
    fsubstr::mpmc::mpmc_tag::Consumer<probes::V046TestUserTag>,
    cc::mpmc_tag::Consumer<probes::V046TestUserTag>>);

// ── 4. Member typedef parity through TestChannel ─────────────────
static_assert(std::is_same_v<typename TestChannel::value_type, int>);
static_assert(std::is_same_v<typename TestChannel::user_tag,
                             probes::V046TestUserTag>);
static_assert(std::is_same_v<typename TestChannel::whole_tag,
                             fsubstr::mpmc::mpmc_tag::Whole<probes::V046TestUserTag>>);
static_assert(std::is_same_v<typename TestChannel::producer_tag,
                             fsubstr::mpmc::mpmc_tag::Producer<probes::V046TestUserTag>>);
static_assert(std::is_same_v<typename TestChannel::consumer_tag,
                             fsubstr::mpmc::mpmc_tag::Consumer<probes::V046TestUserTag>>);

// ── 5. channel_capacity value parity ─────────────────────────────
static_assert(TestChannel::channel_capacity == 64);

// ── 6. MpmcChannelSessionSurface admits the representative channel
static_assert(fsubstr::mpmc::MpmcChannelSessionSurface<TestChannel>);

// ═══════════════════════════════════════════════════════════════════
// ── Runtime witnesses ────────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════

// Construct + mint root permission via fixy::substr::mpmc::mpmc_tag::Whole.
// mint_permission_root<Tag>() (no-ctx form) is valid here because the
// MPMC tag tree's permission_row<Tag> is empty (channel handles are
// EmptyPermSet — no wire-permission transfer; the fractional Pool
// state lives inside the channel, not the Permission tokens).
static void test_runtime_construct_and_split() {
    TestChannel ch{};
    auto whole = cs::mint_permission_root<TestChannel::whole_tag>();
    auto [prod_perm, cons_perm] = cs::mint_permission_split<
        TestChannel::producer_tag,
        TestChannel::consumer_tag>(std::move(whole));
    // MPMC factories take SharedPermission<*_tag>, derived from the
    // Linear Permission<*_tag> via permission_share through a Pool.
    // For a single-handle smoke test, lend from the channel's pool
    // directly (substrate API): producer() / consumer() factories
    // return std::optional<Handle> because the Pool may be exhausted.
    auto p_opt = ch.producer();
    auto c_opt = ch.consumer();
    if (!p_opt) std::abort();
    if (!c_opt) std::abort();
    (void)prod_perm;
    (void)cons_perm;
}

// Producer/consumer endpoint round-trip — push N, pop N, verify FIFO order.
static void test_runtime_push_pop_roundtrip() {
    TestChannel ch{};
    auto p_opt = ch.producer();
    auto c_opt = ch.consumer();
    if (!p_opt) std::abort();
    if (!c_opt) std::abort();

    constexpr int N = 16;  // safely under capacity (64)
    for (int i = 0; i < N; ++i) {
        if (!p_opt->try_push(i * 100 + 7)) std::abort();
    }
    for (int i = 0; i < N; ++i) {
        std::optional<int> r = c_opt->try_pop();
        if (!r) std::abort();
        if (*r != i * 100 + 7) std::abort();
    }
    // Drained.
    if (c_opt->try_pop()) std::abort();
}

// Capacity passthrough (TestChannel::channel_capacity == 64).
static void test_runtime_capacity_constant() {
    static_assert(TestChannel::channel_capacity == 64);
    volatile std::size_t cap = TestChannel::channel_capacity;
    if (cap != 64) std::abort();
}

// Substrate-pointer identity — fsubstr::mpmc::PermissionedMpmcChannel
// alias produces EXACTLY the substrate type, not a fixy-side wrapper.
static void test_runtime_substrate_identity() {
    static_assert(std::is_same_v<
        TestChannel,
        cc::PermissionedMpmcChannel<int, 64, probes::V046TestUserTag>>);
    // Confirm at runtime that an instance constructs and self-asserts
    // identity via Pinned base.
    TestChannel ch{};
    cc::PermissionedMpmcChannel<int, 64, probes::V046TestUserTag>* via_sub = &ch;
    TestChannel* via_fixy = via_sub;  // implicit ptr conversion only works if types match
    if (via_fixy != via_sub) std::abort();
}

// ProtocolType aliases unchanged from pre-V-046 surface — re-verify
// at runtime scope (compile-time witness lives in Substr.h's U-103
// block already; this is a runtime-callsite parity rail).
static void test_runtime_protocol_aliases_unchanged() {
    using FixyProd = fsubstr::mpmc::ProducerProto<int>;
    using FixyCons = fsubstr::mpmc::ConsumerProto<int>;
    using SubsProd = ::crucible::safety::proto::mpmc_channel_session::ProducerProto<int>;
    using SubsCons = ::crucible::safety::proto::mpmc_channel_session::ConsumerProto<int>;
    static_assert(std::is_same_v<FixyProd, SubsProd>);
    static_assert(std::is_same_v<FixyCons, SubsCons>);
}

// ═══════════════════════════════════════════════════════════════════
// ── Driver ────────────────────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════

int main() {
    test_runtime_construct_and_split();
    test_runtime_push_pop_roundtrip();
    test_runtime_capacity_constant();
    test_runtime_substrate_identity();
    test_runtime_protocol_aliases_unchanged();
    std::printf("test_fixy_substr_mpmc_permissioned_mpmc_channel: "
                "5/5 runtime witnesses passed\n");
    return 0;
}
