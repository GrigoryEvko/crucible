// ── test_fixy_substr_chaselev_permissioned_chase_lev_deque — V-047 sentinel TU
//
// Forces evaluation of the header-internal static_asserts in
// `include/crucible/fixy/Substr.h` v047:: block under the project's
// warnings-as-errors flags (per
// feedback_header_only_static_assert_blind_spot.md), plus runtime
// witnesses on top of the compile-time identity sentinels.
//
// Covers the V-047 ChaseLev substrate-direct surface additions:
//   * PermissionedChaseLevDeque<T, Cap, UserTag>          — substrate alias
//   * DequeValue<T>                                       — concept re-export
//   * deque_tag::{Whole,Owner,Thief}<UserTag>             — tag tree
//   (ChaseLevSessionSurface already shipped pre-V-047)
//   (mint_chaselev_* / mint_owner_session / mint_thief_session
//    already shipped pre-V-047 via using-decl)
//
// ChaseLev structural notes (vs V-045 SPSC / V-046 MPMC):
//   * Owner side is LINEAR — owner() consumes Permission<owner_tag>&&
//     and returns a BARE OwnerHandle (mirror of SPSC).  Exactly-one
//     contract holds because CL's push_bottom / pop_bottom is
//     single-owner-only.
//   * Thief side is FRACTIONAL via internally-minted thief pool.
//     thief() takes NO Permission argument and returns
//     std::optional<ThiefHandle> (mirror of MPMC; pool may be in
//     exclusive mode via with_drained_access).
//   * The thief-pool root is minted inside the substrate ctor — the
//     caller mints ONLY the linear Owner Permission externally.

#include <crucible/fixy/Substr.h>

#include <crucible/concurrent/ChaseLevDeque.h>
#include <crucible/concurrent/PermissionedChaseLevDeque.h>
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
// ── Synthetic UserTag for V-047 fixtures ─────────────────────────
// ═══════════════════════════════════════════════════════════════════

namespace probes {

// TU-local tag so PermissionedChaseLevDeque.h's generic specialization
// picks up the fresh (Whole, Owner, Thief) triple via the
// UserTag-parameterized templates.
struct V047TestUserTag {};

}  // namespace probes

using TestDeque =
    fsubstr::chaselev::PermissionedChaseLevDeque<int, 64, probes::V047TestUserTag>;

// ═══════════════════════════════════════════════════════════════════
// ── Compile-time witnesses (re-state at TU scope) ────────────────
// ═══════════════════════════════════════════════════════════════════

// ── 1. Substrate alias identity ─────────────────────────────────
static_assert(std::is_same_v<
    TestDeque,
    cc::PermissionedChaseLevDeque<int, 64, probes::V047TestUserTag>>,
    "fixy::substr::chaselev::PermissionedChaseLevDeque must alias the substrate.");

// ── 2. DequeValue concept re-export parity ───────────────────────
static_assert(fsubstr::chaselev::DequeValue<int>);
static_assert(fsubstr::chaselev::DequeValue<int> == cc::DequeValue<int>);
// Negative — a non-trivially-copyable type fails the concept on BOTH paths.
struct NonDequeValue { ~NonDequeValue() {} };  // non-trivial dtor
static_assert(!fsubstr::chaselev::DequeValue<NonDequeValue>);
static_assert(!cc::DequeValue<NonDequeValue>);

// ── 3. Tag template identity — fixy path === concurrent path ─────
static_assert(std::is_same_v<
    fsubstr::chaselev::deque_tag::Whole<probes::V047TestUserTag>,
    cc::deque_tag::Whole<probes::V047TestUserTag>>);
static_assert(std::is_same_v<
    fsubstr::chaselev::deque_tag::Owner<probes::V047TestUserTag>,
    cc::deque_tag::Owner<probes::V047TestUserTag>>);
static_assert(std::is_same_v<
    fsubstr::chaselev::deque_tag::Thief<probes::V047TestUserTag>,
    cc::deque_tag::Thief<probes::V047TestUserTag>>);

// ── 4. Member typedef parity through TestDeque ───────────────────
static_assert(std::is_same_v<typename TestDeque::value_type, int>);
static_assert(std::is_same_v<typename TestDeque::user_tag,
                             probes::V047TestUserTag>);
static_assert(std::is_same_v<typename TestDeque::whole_tag,
                             fsubstr::chaselev::deque_tag::Whole<probes::V047TestUserTag>>);
static_assert(std::is_same_v<typename TestDeque::owner_tag,
                             fsubstr::chaselev::deque_tag::Owner<probes::V047TestUserTag>>);
static_assert(std::is_same_v<typename TestDeque::thief_tag,
                             fsubstr::chaselev::deque_tag::Thief<probes::V047TestUserTag>>);

// ── 5. deque_capacity value parity ───────────────────────────────
static_assert(TestDeque::deque_capacity == 64);

// ── 6. ChaseLevSessionSurface admits the representative deque ─────
static_assert(fsubstr::chaselev::ChaseLevSessionSurface<TestDeque>);

// ═══════════════════════════════════════════════════════════════════
// ── Runtime witnesses ────────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════

// Construct + mint owner permission via fixy::substr::chaselev::deque_tag::Owner.
// The thief pool root is minted INSIDE the substrate ctor; the user
// mints only the Owner Permission externally.  mint_permission_root
// (no-ctx form) is valid here because the ChaseLev tag tree's
// permission_row<Tag> is empty (deque handles are EmptyPermSet — no
// wire-permission transfer; the fractional Thief pool state lives
// inside the deque, not the Permission tokens).
static void test_runtime_construct_and_owner() {
    TestDeque deque{};
    auto owner_perm = cs::mint_permission_root<TestDeque::owner_tag>();
    auto owner = deque.owner(std::move(owner_perm));
    // OwnerHandle is move-only; existence proves the Linear Permission
    // was consumed.  size_approx is 0 on a fresh deque.
    if (owner.size_approx() != 0) std::abort();
    if (!owner.empty_approx()) std::abort();
}

// Owner push/pop round-trip — push N, pop N, verify LIFO order.
// CL's bottom-side push/pop is LIFO (owner uses the deque as a stack).
static void test_runtime_owner_push_pop_lifo() {
    TestDeque deque{};
    auto owner = deque.owner(
        cs::mint_permission_root<TestDeque::owner_tag>());

    constexpr int N = 16;  // safely under capacity (64)
    for (int i = 0; i < N; ++i) {
        if (!owner.try_push(i * 100 + 7)) std::abort();
    }
    // Pop from bottom — LIFO: most recently pushed comes out first.
    for (int i = N - 1; i >= 0; --i) {
        std::optional<int> r = owner.try_pop();
        if (!r) std::abort();
        if (*r != i * 100 + 7) std::abort();
    }
    // Drained.
    if (owner.try_pop()) std::abort();
}

// Owner pushes, Thief steals — verify FIFO order on the steal side.
// CL's top-side steal is FIFO (thieves observe oldest-first).
static void test_runtime_owner_push_thief_steal_fifo() {
    TestDeque deque{};
    auto owner = deque.owner(
        cs::mint_permission_root<TestDeque::owner_tag>());

    constexpr int N = 8;
    for (int i = 0; i < N; ++i) {
        if (!owner.try_push(i * 1000 + 13)) std::abort();
    }

    // Lend a thief from the internal pool.
    auto thief_opt = deque.thief();
    if (!thief_opt) std::abort();
    auto thief = std::move(*thief_opt);

    // Steal from top — FIFO: oldest pushed comes out first.
    // The CAS race can spuriously return nullopt on contention even
    // when the deque is non-empty; retry per CL's spec.
    for (int i = 0; i < N; ++i) {
        std::optional<int> r;
        for (int retry = 0; retry < 16 && !r; ++retry) {
            r = thief.try_steal();
        }
        if (!r) std::abort();
        if (*r != i * 1000 + 13) std::abort();
    }
}

// Capacity passthrough (TestDeque::deque_capacity == 64).
static void test_runtime_capacity_constant() {
    static_assert(TestDeque::deque_capacity == 64);
    volatile std::size_t cap = TestDeque::deque_capacity;
    if (cap != 64) std::abort();
}

// Substrate-pointer identity — fsubstr::chaselev::PermissionedChaseLevDeque
// alias produces EXACTLY the substrate type, not a fixy-side wrapper.
static void test_runtime_substrate_identity() {
    static_assert(std::is_same_v<
        TestDeque,
        cc::PermissionedChaseLevDeque<int, 64, probes::V047TestUserTag>>);
    // Confirm at runtime that an instance constructs and self-asserts
    // identity via Pinned base.
    TestDeque deque{};
    cc::PermissionedChaseLevDeque<int, 64, probes::V047TestUserTag>* via_sub = &deque;
    TestDeque* via_fixy = via_sub;  // implicit ptr conversion only works if types match
    if (via_fixy != via_sub) std::abort();
}

// ProtocolType aliases unchanged from pre-V-047 surface — re-verify
// at runtime scope (compile-time witness lives in Substr.h's U-103
// block already; this is a runtime-callsite parity rail).
static void test_runtime_protocol_aliases_unchanged() {
    using FixyOwner = fsubstr::chaselev::OwnerProto<int>;
    using FixyThief = fsubstr::chaselev::ThiefProto<int, TestDeque::thief_tag>;
    using SubsOwner = ::crucible::safety::proto::chaselev_session::OwnerProto<int>;
    using SubsThief = ::crucible::safety::proto::chaselev_session::ThiefProto<
        int, TestDeque::thief_tag>;
    static_assert(std::is_same_v<FixyOwner, SubsOwner>);
    static_assert(std::is_same_v<FixyThief, SubsThief>);
}

// ═══════════════════════════════════════════════════════════════════
// ── Driver ────────────────────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════

int main() {
    test_runtime_construct_and_owner();
    test_runtime_owner_push_pop_lifo();
    test_runtime_owner_push_thief_steal_fifo();
    test_runtime_capacity_constant();
    test_runtime_substrate_identity();
    test_runtime_protocol_aliases_unchanged();
    std::printf("test_fixy_substr_chaselev_permissioned_chase_lev_deque: "
                "6/6 runtime witnesses passed\n");
    return 0;
}
