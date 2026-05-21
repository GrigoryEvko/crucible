// ── test_fixy_substr_chainedge_permissioned_chain_edge —
// V-052 sentinel TU
//
// Forces evaluation of the header-internal static_asserts in
// `include/crucible/fixy/Substr.h` v052:: block under the project's
// warnings-as-errors flags (per
// feedback_header_only_static_assert_blind_spot.md), plus runtime
// witnesses on top of the compile-time identity sentinels.
//
// Covers the V-052 ChainEdge substrate-direct surface additions:
//   * PermissionedChainEdge<Backend = VendorBackend::CPU, UserTag = void>
//                                                           — substrate alias
//   * ::crucible::concurrent::VendorBackend                  — vendor enum
//                                                             (V-052-unique
//                                                              cardinality bump)
//   * chainedge_tag::{Whole, Signaler, Waiter}<UserTag>      — tag tree
//   (ChainEdgeSessionSurface already shipped pre-V-052 as concept template)
//   (mint_chainedge_{signaler,waiter}[_session] already shipped
//    pre-V-052 via using-decl)
//
// ChainEdge structural notes:
//   * EIGHTH and FINAL cell of the V-045..V-052 channel-permission
//     family — linear × linear one-shot GPU semaphore (single signaler,
//     single waiter, exactly one signal per edge between
//     reset_under_quiescence cycles).
//   * SHAPE: 1 × 1, NO grid, NO key-priority — but unique on a
//     VendorBackend axis (CPU oracle vs NV / AMD / TPU / TRN native
//     semaphore).  Pre-real-backend, every vendor delegates to the CPU
//     oracle (release store / acquire load) — see mimic/Semaphore.h.
//   * Substrate is Pinned (delete copy + delete move) — the atomic
//     counter ON THE SEMAPHORE IS the channel identity.
//   * Signaler/Waiter split via the standard CSL frame rule —
//     mint_permission_root<whole_tag> then
//     mint_permission_split<signaler_tag, waiter_tag>(whole).
//   * V-052-unique cardinality bump: the VendorBackend enum re-export
//     (no other substrate in V-045..V-052 is parameterized on vendor
//     axis — MetaLog/SPSC/MPMC/ChaseLev/CalendarGrid* are all single-
//     vendor SPSC patterns).

#include <crucible/fixy/Substr.h>

#include <crucible/concurrent/ChainEdge.h>
#include <crucible/concurrent/PermissionedChainEdge.h>
#include <crucible/permissions/Permission.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <type_traits>
#include <utility>

namespace fsubstr = ::crucible::fixy::substr;
namespace cc      = ::crucible::concurrent;
namespace cs      = ::crucible::safety;

// ═══════════════════════════════════════════════════════════════════
// ── Synthetic UserTag for V-052 fixtures ─────────────────────────
// ═══════════════════════════════════════════════════════════════════

namespace probes {

// TU-local tag so PermissionedChainEdge.h's specializations pick up a
// fresh (Whole, Signaler, Waiter) triple via the UserTag-parameterized
// templates.
struct V052TestUserTag {};

}  // namespace probes

// ═══════════════════════════════════════════════════════════════════
// ── Fixture aliases ──────────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════

using TestEdge =
    fsubstr::chainedge::PermissionedChainEdge<cc::VendorBackend::CPU,
                                              probes::V052TestUserTag>;
using TestEdgeNv =
    fsubstr::chainedge::PermissionedChainEdge<cc::VendorBackend::NV,
                                              probes::V052TestUserTag>;

// ═══════════════════════════════════════════════════════════════════
// ── Compile-time witnesses (re-state at TU scope) ────────────────
// ═══════════════════════════════════════════════════════════════════

// ── 1. Substrate alias identity — default Backend (CPU) ─────────
static_assert(std::is_same_v<
    TestEdge,
    cc::PermissionedChainEdge<cc::VendorBackend::CPU,
                              probes::V052TestUserTag>>,
    "fixy::substr::chainedge::PermissionedChainEdge<CPU,_> must alias "
    "the substrate.");

// ── 1b. Substrate alias identity — non-default Backend (NV) ──────
static_assert(std::is_same_v<
    TestEdgeNv,
    cc::PermissionedChainEdge<cc::VendorBackend::NV,
                              probes::V052TestUserTag>>,
    "fixy::substr::chainedge::PermissionedChainEdge<NV,_> must alias "
    "the substrate's non-default-backend variant.");

// ── 2. VendorBackend enum identity (V-052-unique cardinality bump)
static_assert(std::is_same_v<
    fsubstr::chainedge::VendorBackend,
    cc::VendorBackend>,
    "fixy::substr::chainedge::VendorBackend must alias the substrate's "
    "VendorBackend.");
// Per-enumerator value parity — V-052-unique surface check.
static_assert(static_cast<int>(fsubstr::chainedge::VendorBackend::CPU) ==
              static_cast<int>(cc::VendorBackend::CPU));
static_assert(static_cast<int>(fsubstr::chainedge::VendorBackend::NV) ==
              static_cast<int>(cc::VendorBackend::NV));
static_assert(static_cast<int>(fsubstr::chainedge::VendorBackend::AMD) ==
              static_cast<int>(cc::VendorBackend::AMD));
static_assert(static_cast<int>(fsubstr::chainedge::VendorBackend::TPU) ==
              static_cast<int>(cc::VendorBackend::TPU));
static_assert(static_cast<int>(fsubstr::chainedge::VendorBackend::TRN) ==
              static_cast<int>(cc::VendorBackend::TRN));

// ── 3. Tag tree identity ────────────────────────────────────────
static_assert(std::is_same_v<
    fsubstr::chainedge::chainedge_tag::Whole<probes::V052TestUserTag>,
    cc::chainedge_tag::Whole<probes::V052TestUserTag>>);
static_assert(std::is_same_v<
    fsubstr::chainedge::chainedge_tag::Signaler<probes::V052TestUserTag>,
    cc::chainedge_tag::Signaler<probes::V052TestUserTag>>);
static_assert(std::is_same_v<
    fsubstr::chainedge::chainedge_tag::Waiter<probes::V052TestUserTag>,
    cc::chainedge_tag::Waiter<probes::V052TestUserTag>>);

// ── 4. Tag identity propagates through TestEdge member typedefs ─
static_assert(std::is_same_v<
    typename TestEdge::whole_tag,
    fsubstr::chainedge::chainedge_tag::Whole<probes::V052TestUserTag>>);
static_assert(std::is_same_v<
    typename TestEdge::signaler_tag,
    fsubstr::chainedge::chainedge_tag::Signaler<probes::V052TestUserTag>>);
static_assert(std::is_same_v<
    typename TestEdge::waiter_tag,
    fsubstr::chainedge::chainedge_tag::Waiter<probes::V052TestUserTag>>);

// ── 5. ChainEdgeSessionSurface admits the representative edges.
static_assert(
    fsubstr::chainedge::ChainEdgeSessionSurface<TestEdge>);
static_assert(
    fsubstr::chainedge::ChainEdgeSessionSurface<TestEdgeNv>);

// ── 6. value_type identity — Signal == SemaphoreSignal ───────────
static_assert(std::is_same_v<
    typename TestEdge::value_type,
    cc::SemaphoreSignal>);
static_assert(std::is_same_v<
    typename TestEdge::value_type,
    fsubstr::chainedge::Signal>);

// ── 7. Backend constant passes through cleanly through fixy alias.
static_assert(TestEdge::backend == cc::VendorBackend::CPU);
static_assert(TestEdgeNv::backend == cc::VendorBackend::NV);

// ── 8. Protocol aliases preserved (pre-existing re-exports).
static_assert(std::is_same_v<
    fsubstr::chainedge::SignalerProto,
    ::crucible::safety::proto::chainedge_session::SignalerProto>);
static_assert(std::is_same_v<
    fsubstr::chainedge::WaiterProto,
    ::crucible::safety::proto::chainedge_session::WaiterProto>);

// ═══════════════════════════════════════════════════════════════════
// ── Runtime fixture helpers ──────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════

namespace {

int total_passed = 0;
int total_failed = 0;

#define CRUCIBLE_TEST_REQUIRE(cond)                                  \
    do {                                                              \
        if (!(cond)) {                                                \
            std::fprintf(stderr,                                      \
                "  REQUIRE FAILED: %s @ %s:%d\n",                     \
                #cond, __FILE__, __LINE__);                           \
            ++total_failed;                                           \
            return;                                                   \
        }                                                             \
    } while (0)

template <typename Body>
void run_test(const char* name, Body body) {
    std::fprintf(stderr, "  %s ... ", name);
    int before = total_failed;
    body();
    if (total_failed == before) {
        ++total_passed;
        std::fprintf(stderr, "OK\n");
    } else {
        std::fprintf(stderr, "FAILED\n");
    }
}

// Canonical V-052 fixture: build a TestEdge with deterministic plan/
// edge IDs and a non-default signal_value so we exercise the strict
// `matches_expected_signal_` substrate gate during runtime asserts.
struct EdgeFixture {
    static constexpr cc::PlanId       upstream{42};
    static constexpr cc::PlanId       downstream{84};
    static constexpr cc::ChainEdgeId  edge_id{17};
    static constexpr std::uint64_t    signal_value = 7;
};

// Mint a fresh (Whole → Signaler + Waiter) triple via the fixy::
// substrate-side tag aliases.  Linear × linear (no grid), so the
// standard mint_permission_split rail applies — mirrors the canonical
// pattern at test_chainedge_session.cpp:50-54.
template <typename Edge>
[[nodiscard]] auto fresh_chainedge_perms() {
    auto whole = cs::mint_permission_root<typename Edge::whole_tag>();
    return cs::mint_permission_split<typename Edge::signaler_tag,
                                     typename Edge::waiter_tag>(
        std::move(whole));
}

}  // namespace

// ═══════════════════════════════════════════════════════════════════
// ── Runtime witnesses ────────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════

// Construct PermissionedChainEdge through fixy:: alias, mint linear
// Signaler / Waiter permissions through the fixy:: tag tree, and
// derive handles.  Verifies the surface composes end-to-end through
// fixy:: name lookups (no descent into ::crucible::concurrent:: at
// the caller site).
static void test_runtime_construct_and_handles() {
    TestEdge edge{EdgeFixture::upstream,
                  EdgeFixture::downstream,
                  EdgeFixture::edge_id,
                  EdgeFixture::signal_value};

    auto [sp, wp] = fresh_chainedge_perms<TestEdge>();
    auto signaler = edge.signaler(std::move(sp));
    auto waiter   = edge.waiter(std::move(wp));
    (void)signaler;
    (void)waiter;

    // Counter starts at zero — no signal has been emitted yet.
    CRUCIBLE_TEST_REQUIRE(signaler.current_value() == 0);
    CRUCIBLE_TEST_REQUIRE(waiter.current_value() == 0);
}

// Linear signal → wait one-shot through fixy:: surface.  Exercises
// SignalerHandle::signal() (return-value form) and WaiterHandle::
// try_wait through the fixy:: alias chain.
static void test_runtime_signal_wait_round_trip() {
    TestEdge edge{EdgeFixture::upstream,
                  EdgeFixture::downstream,
                  EdgeFixture::edge_id,
                  EdgeFixture::signal_value};

    auto [sp, wp] = fresh_chainedge_perms<TestEdge>();
    auto signaler = edge.signaler(std::move(sp));
    auto waiter   = edge.waiter(std::move(wp));

    // Pre-signal: try_wait must fail (counter = 0 < expected_value).
    const cc::SemaphoreSignal expected = waiter.expected_signal();
    CRUCIBLE_TEST_REQUIRE(!waiter.try_wait(expected));
    CRUCIBLE_TEST_REQUIRE(waiter.current_value() == 0);

    // Emit signal — counter advances to signal_value.
    const cc::SemaphoreSignal emitted = signaler.signal();
    CRUCIBLE_TEST_REQUIRE(emitted.value == EdgeFixture::signal_value);
    CRUCIBLE_TEST_REQUIRE(signaler.current_value() ==
                          EdgeFixture::signal_value);

    // Post-signal: try_wait succeeds, counter still at signal_value.
    CRUCIBLE_TEST_REQUIRE(waiter.try_wait(emitted));
    CRUCIBLE_TEST_REQUIRE(waiter.current_value() ==
                          EdgeFixture::signal_value);
}

// Expected-signal fields propagate through the substrate gate.  This
// witness pins the strict `matches_expected_signal_` check — a
// SemaphoreSignal whose fields don't match the edge is silently
// dropped (signaler.signal(wrong) becomes a no-op).  V-052-relevant
// because the V-052 cardinality bump (VendorBackend enum) IS one of
// the fields the substrate checks.
static void test_runtime_expected_signal_propagates() {
    TestEdge edge{EdgeFixture::upstream,
                  EdgeFixture::downstream,
                  EdgeFixture::edge_id,
                  EdgeFixture::signal_value};

    auto [sp, wp] = fresh_chainedge_perms<TestEdge>();
    auto signaler = edge.signaler(std::move(sp));
    auto waiter   = edge.waiter(std::move(wp));

    const cc::SemaphoreSignal sig = signaler.expected_signal();
    CRUCIBLE_TEST_REQUIRE(sig.edge       == EdgeFixture::edge_id);
    CRUCIBLE_TEST_REQUIRE(sig.upstream   == EdgeFixture::upstream);
    CRUCIBLE_TEST_REQUIRE(sig.downstream == EdgeFixture::downstream);
    CRUCIBLE_TEST_REQUIRE(sig.value      == EdgeFixture::signal_value);
    // Backend field IS the V-052-unique cardinality-bump witness.
    CRUCIBLE_TEST_REQUIRE(sig.backend    == cc::VendorBackend::CPU);

    // Symmetric check from the waiter side.
    const cc::SemaphoreSignal wsig = waiter.expected_signal();
    CRUCIBLE_TEST_REQUIRE(wsig.edge       == sig.edge);
    CRUCIBLE_TEST_REQUIRE(wsig.upstream   == sig.upstream);
    CRUCIBLE_TEST_REQUIRE(wsig.downstream == sig.downstream);
    CRUCIBLE_TEST_REQUIRE(wsig.value      == sig.value);
    CRUCIBLE_TEST_REQUIRE(wsig.backend    == sig.backend);

    // A mismatched signal must silently NOT advance the counter — the
    // substrate gate rejects identity-violating signals.
    cc::SemaphoreSignal forged = sig;
    forged.edge = cc::ChainEdgeId{forged.edge.raw() + 1};
    signaler.signal(forged);
    CRUCIBLE_TEST_REQUIRE(signaler.current_value() == 0);
    CRUCIBLE_TEST_REQUIRE(!waiter.try_wait(sig));
}

// reset_under_quiescence round trip — consumes the Whole Permission
// and returns it, drops the counter back to zero, allowing a fresh
// signal/wait cycle.  Validates the V-052 reset path through the
// substrate (no fixy-side mint factory exists for reset — it is a
// substrate method consuming the Whole Permission).
static void test_runtime_reset_under_quiescence() {
    TestEdge edge{EdgeFixture::upstream,
                  EdgeFixture::downstream,
                  EdgeFixture::edge_id,
                  EdgeFixture::signal_value};

    // Cycle 1: signal → wait succeeds.
    auto whole1 = cs::mint_permission_root<TestEdge::whole_tag>();
    auto [sp1, wp1] =
        cs::mint_permission_split<TestEdge::signaler_tag,
                                  TestEdge::waiter_tag>(std::move(whole1));
    auto signaler1 = edge.signaler(std::move(sp1));
    auto waiter1   = edge.waiter(std::move(wp1));
    const cc::SemaphoreSignal sig1 = signaler1.signal();
    CRUCIBLE_TEST_REQUIRE(waiter1.try_wait(sig1));
    CRUCIBLE_TEST_REQUIRE(waiter1.current_value() ==
                          EdgeFixture::signal_value);

    // Reset under quiescence — counter back to zero.  Requires the
    // Whole permission to be re-minted (Signaler+Waiter Permissions
    // are scoped to handles which we drop at scope exit).
    auto whole2 = cs::mint_permission_root<TestEdge::whole_tag>();
    whole2 = edge.reset_under_quiescence(std::move(whole2));

    // Cycle 2: counter is zero, fresh signal/wait works.
    auto [sp2, wp2] =
        cs::mint_permission_split<TestEdge::signaler_tag,
                                  TestEdge::waiter_tag>(std::move(whole2));
    auto signaler2 = edge.signaler(std::move(sp2));
    auto waiter2   = edge.waiter(std::move(wp2));
    CRUCIBLE_TEST_REQUIRE(waiter2.current_value() == 0);
    CRUCIBLE_TEST_REQUIRE(!waiter2.try_wait(waiter2.expected_signal()));

    const cc::SemaphoreSignal sig2 = signaler2.signal();
    CRUCIBLE_TEST_REQUIRE(waiter2.try_wait(sig2));
}

// Non-default backend variant instantiates AND signals correctly
// through the fixy:: alias chain.  Under the current stubbed
// mimic::Semaphore, every vendor delegates to the CPU oracle — but
// the substrate carries the VendorBackend AS PART of the signal
// identity, so a NV-tagged edge produces NV-tagged signals.  This is
// the V-052-unique surface witness.
static void test_runtime_nv_backend_variant() {
    TestEdgeNv edge_nv{EdgeFixture::upstream,
                       EdgeFixture::downstream,
                       EdgeFixture::edge_id,
                       EdgeFixture::signal_value};

    auto [sp, wp] = fresh_chainedge_perms<TestEdgeNv>();
    auto signaler_nv = edge_nv.signaler(std::move(sp));
    auto waiter_nv   = edge_nv.waiter(std::move(wp));

    const cc::SemaphoreSignal sig = signaler_nv.expected_signal();
    CRUCIBLE_TEST_REQUIRE(sig.backend == cc::VendorBackend::NV);

    const cc::SemaphoreSignal emitted = signaler_nv.signal();
    CRUCIBLE_TEST_REQUIRE(emitted.backend == cc::VendorBackend::NV);
    CRUCIBLE_TEST_REQUIRE(waiter_nv.try_wait(emitted));
    CRUCIBLE_TEST_REQUIRE(waiter_nv.current_value() ==
                          EdgeFixture::signal_value);
}

// Identity sanity at runtime — PermissionedChainEdge is Pinned (no
// copy/move), backend constant is the template Backend argument,
// value_type IS SemaphoreSignal exposed via Signal alias.
static void test_runtime_substrate_identity() {
    // Pinned discipline: every special member is deleted.
    static_assert(!std::is_copy_constructible_v<TestEdge>);
    static_assert(!std::is_move_constructible_v<TestEdge>);
    static_assert(!std::is_copy_assignable_v<TestEdge>);
    static_assert(!std::is_move_assignable_v<TestEdge>);

    // Backend constant equals the Backend template argument.
    static_assert(TestEdge::backend == cc::VendorBackend::CPU);
    static_assert(TestEdgeNv::backend == cc::VendorBackend::NV);

    // value_type is SemaphoreSignal, exposed via fixy::Signal alias.
    static_assert(std::is_same_v<typename TestEdge::value_type,
                                 cc::SemaphoreSignal>);
    static_assert(std::is_same_v<typename TestEdge::value_type,
                                 fsubstr::chainedge::Signal>);
}

int main() {
    std::fprintf(stderr,
        "test_fixy_substr_chainedge_permissioned_chain_edge: "
        "starting V-052 runtime witnesses\n");

    run_test("construct_and_handles",
             test_runtime_construct_and_handles);
    run_test("signal_wait_round_trip",
             test_runtime_signal_wait_round_trip);
    run_test("expected_signal_propagates",
             test_runtime_expected_signal_propagates);
    run_test("reset_under_quiescence",
             test_runtime_reset_under_quiescence);
    run_test("nv_backend_variant",
             test_runtime_nv_backend_variant);
    run_test("substrate_identity",
             test_runtime_substrate_identity);

    std::fprintf(stderr,
        "test_fixy_substr_chainedge_permissioned_chain_edge: "
        "%d/%d runtime witnesses passed\n",
        total_passed, total_passed + total_failed);

    return total_failed == 0 ? 0 : 1;
}
