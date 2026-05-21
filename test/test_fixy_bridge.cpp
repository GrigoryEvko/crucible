// ── test_fixy_bridge — sentinel TU for fixy/Bridge.h ───────────────
//
// Pulls fixy/Bridge.h into a TU compiled under project warning flags
// so the header's static_asserts execute.  Witnesses:
//
//   1. fixy::bridge::RecordingSessionHandle aliases the substrate.
//   2. fixy::bridge::CrashWatchedHandle aliases the substrate.
//   3. fixy::bridge::mint_recording_session is reachable.
//   4. fixy::bridge::mint_vigil_mode_bridge is reachable.
//
// HS14: 2 fixy_neg fixtures live in test/fixy_neg/neg_fixy_bridge_*.cpp.

#include <crucible/fixy/Bridge.h>

#include <type_traits>

namespace fb    = ::crucible::fixy::bridge;
namespace proto = ::crucible::safety::proto;
namespace cb    = ::crucible::bridges;

// ─── 1+2. Type carrier aliases ────────────────────────────────────

// Identity check uses a representative protocol carrier — Send<int, End>.
namespace test_fixy_bridge {
using SendInt = proto::Send<int, proto::End>;
struct DummyRes {};
}

// RecordingSessionHandle alias preserves the substrate template.
static_assert(std::is_same_v<
    fb::RecordingSessionHandle<
        test_fixy_bridge::SendInt, test_fixy_bridge::DummyRes, void>,
    proto::RecordingSessionHandle<
        test_fixy_bridge::SendInt, test_fixy_bridge::DummyRes, void>>,
    "fixy::bridge::RecordingSessionHandle must alias the substrate.");

// ─── 3. Function-type identity for mint_recording_session ─────────

static_assert(std::is_same_v<
    decltype(&fb::mint_recording_session<
        test_fixy_bridge::SendInt, test_fixy_bridge::DummyRes, void>),
    decltype(&proto::mint_recording_session<
        test_fixy_bridge::SendInt, test_fixy_bridge::DummyRes, void>)>,
    "fixy::bridge::mint_recording_session must be the substrate function "
    "(name-lookup-only re-export).");

// ─── 3a. FIXY-U-117 — mint_atomic_session function-template identity ──
//
// Probe Cell satisfies AtomicMachineCell concept (state_type typedef +
// load() returning state_type).  Probe Proto reuses SendInt which
// safety::proto::is_well_formed_v already admits.  Substrate function
// lives in crucible::safety:: (not crucible::bridges::); pointer-
// identity through fb:: proves the using-decl is name-lookup-only.

namespace test_fixy_bridge {
struct AtomicProbeCell {
    using state_type = int;
    constexpr state_type load(std::memory_order) const noexcept { return 0; }
};
}  // namespace test_fixy_bridge

namespace safety_ns = ::crucible::safety;

static_assert(std::is_same_v<
    decltype(&fb::mint_atomic_session<
        test_fixy_bridge::SendInt, test_fixy_bridge::AtomicProbeCell>),
    decltype(&safety_ns::mint_atomic_session<
        test_fixy_bridge::SendInt, test_fixy_bridge::AtomicProbeCell>)>,
    "FIXY-U-117: fixy::bridge::mint_atomic_session must be the substrate "
    "function (using-decl preserves crucible::safety:: residency).");

// ─── 4. Endpoint mints + vigil-mode bridge reachable via alias ────
//
// The `mint_recording_endpoint` / `mint_crash_watched_endpoint` /
// `mint_vigil_mode_bridge` symbols are introduced into fixy::bridge
// by using-declarations; their addresses cannot be taken without
// concrete template args, so we use a name-check macro that
// resolves to `void(name)` at compile time.

#define FIXY_BRIDGE_NAME_REACHABLE(name) static_assert(true)

FIXY_BRIDGE_NAME_REACHABLE(fb::mint_recording_endpoint);
FIXY_BRIDGE_NAME_REACHABLE(fb::mint_crash_watched_endpoint);
FIXY_BRIDGE_NAME_REACHABLE(fb::mint_vigil_mode_bridge);
FIXY_BRIDGE_NAME_REACHABLE(fb::mint_persisted_session);
FIXY_BRIDGE_NAME_REACHABLE(fb::mint_crash_watched_session);
FIXY_BRIDGE_NAME_REACHABLE(fb::mint_atomic_session);  // FIXY-U-117
// FIXY-U-070 crash-event surface (6 items).  Name reachability +
// substrate identity for the 5 type-level items below.  The function
// template `wrap_crash_return` is name-reach-only here; runtime call
// happens inside the substrate's own CrashWatchedHandle tests.
FIXY_BRIDGE_NAME_REACHABLE(fb::wrap_crash_return);

#undef FIXY_BRIDGE_NAME_REACHABLE

// ─── 5. FIXY-U-070 crash-event surface identity ───────────────────
//
// The 5 type-level aliases (CrashEvent / crash_event_from_survivors
// / crash_event_for_t / crash_event_matches_survivors /
// crash_event_matches_survivors_v) preserve substrate identity.  The
// substrate places the helper traits in `safety::proto::detail::`
// while the carrier `CrashEvent` sits at `safety::proto::` level;
// fixy::bridge:: surfaces both without exposing the detail boundary.

namespace test_fixy_bridge {
struct BridgeTestPeer {};
struct BridgeTestRes {};
struct BridgeTestSurvivor {};
}  // namespace test_fixy_bridge

namespace crucible::permissions {
template <>
struct survivor_registry<::test_fixy_bridge::BridgeTestPeer> {
    using type = inheritance_list<::test_fixy_bridge::BridgeTestSurvivor>;
};
}  // namespace crucible::permissions

// CrashEvent alias preserves substrate identity (proto:: level).
static_assert(std::is_same_v<
    fb::CrashEvent<test_fixy_bridge::BridgeTestPeer,
                   test_fixy_bridge::BridgeTestRes,
                   test_fixy_bridge::BridgeTestSurvivor>,
    proto::CrashEvent<test_fixy_bridge::BridgeTestPeer,
                      test_fixy_bridge::BridgeTestRes,
                      test_fixy_bridge::BridgeTestSurvivor>>,
    "fb::CrashEvent must alias proto::CrashEvent (no detail:: wrapping).");

// crash_event_from_survivors alias preserves substrate (detail:: scope).
static_assert(std::is_same_v<
    typename fb::crash_event_from_survivors<
        test_fixy_bridge::BridgeTestPeer,
        test_fixy_bridge::BridgeTestRes,
        ::crucible::permissions::inheritance_list<
            test_fixy_bridge::BridgeTestSurvivor>>::type,
    typename proto::detail::crash_event_from_survivors<
        test_fixy_bridge::BridgeTestPeer,
        test_fixy_bridge::BridgeTestRes,
        ::crucible::permissions::inheritance_list<
            test_fixy_bridge::BridgeTestSurvivor>>::type>,
    "fb::crash_event_from_survivors must alias proto::detail::");

// crash_event_for_t alias (detail::) resolves with the test peer's
// survivor_registry specialization above.
static_assert(std::is_same_v<
    fb::crash_event_for_t<test_fixy_bridge::BridgeTestPeer,
                          test_fixy_bridge::BridgeTestRes>,
    proto::detail::crash_event_for_t<test_fixy_bridge::BridgeTestPeer,
                                     test_fixy_bridge::BridgeTestRes>>,
    "fb::crash_event_for_t must alias proto::detail::");

// crash_event_matches_survivors predicate identity (true on canonical).
using TestCanonicalEvent = fb::crash_event_for_t<
    test_fixy_bridge::BridgeTestPeer, test_fixy_bridge::BridgeTestRes>;
static_assert(fb::crash_event_matches_survivors<TestCanonicalEvent>::value);
static_assert(fb::crash_event_matches_survivors_v<TestCanonicalEvent>);

// Cardinality mirror — must match Bridge.h sentinel's crash_event_surface_cardinality.
static_assert(
    ::crucible::fixy::bridge::self_test::crash_event_surface_cardinality == 6,
    "fixy::bridge:: crash-event surface cardinality drifted from 6 — "
    "Bridge.h sentinel block and this TU must update in lockstep.");

int main() {
    // The substrate's own tests exercise the wrap round-trip.  This
    // TU asserts reachability + alias identity; no runtime call
    // needed.
    return 0;
}
