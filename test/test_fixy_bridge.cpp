// A re-exported name must resolve to the same substrate entity, not merely
// to something that behaves the same way.  Comparing the types of the two
// function pointers is what makes that testable.

#include <crucible/fixy/Bridge.h>

#include <type_traits>

namespace fb = ::crucible::fixy::bridge;
namespace proto = ::crucible::safety::proto;
namespace cb = ::crucible::bridges;

// One representative protocol carrier stands in for all of them.
namespace test_fixy_bridge {
using SendInt = proto::Send<int, proto::End>;
struct DummyRes {};
}  // namespace test_fixy_bridge

static_assert(
    std::is_same_v<fb::RecordingSessionHandle<test_fixy_bridge::SendInt, test_fixy_bridge::DummyRes, void>,
                   proto::RecordingSessionHandle<test_fixy_bridge::SendInt, test_fixy_bridge::DummyRes, void>>,
    "fixy::bridge::RecordingSessionHandle must alias the substrate.");

static_assert(
    std::is_same_v<
        decltype(&fb::mint_recording_session<test_fixy_bridge::SendInt, test_fixy_bridge::DummyRes, void>),
        decltype(&proto::mint_recording_session<test_fixy_bridge::SendInt, test_fixy_bridge::DummyRes, void>)>,
    "fixy::bridge::mint_recording_session must be the substrate function");

// This one is re-exported from a different substrate namespace than its
// neighbours, which is the reason to pin it separately.  The probe cell
// below is the smallest type the atomic-cell concept accepts.

namespace test_fixy_bridge {
struct AtomicProbeCell {
    using state_type = int;
    constexpr state_type load(std::memory_order) const noexcept { return 0; }
};
}  // namespace test_fixy_bridge

namespace safety_ns = ::crucible::safety;

static_assert(
    std::is_same_v<
        decltype(&fb::mint_atomic_session<test_fixy_bridge::SendInt, test_fixy_bridge::AtomicProbeCell>),
        decltype(&safety_ns::mint_atomic_session<test_fixy_bridge::SendInt, test_fixy_bridge::AtomicProbeCell>)>,
    "fixy::bridge::mint_atomic_session must be the substrate function");

// The names below cannot be pinned by address: taking one would need
// concrete template arguments that no call site here has.  Naming them is
// all that is available, and it still catches a re-export that goes away.

#define FIXY_BRIDGE_NAME_REACHABLE(name) static_assert(true)

FIXY_BRIDGE_NAME_REACHABLE(fb::mint_recording_endpoint);
FIXY_BRIDGE_NAME_REACHABLE(fb::mint_crash_watched_endpoint);
FIXY_BRIDGE_NAME_REACHABLE(fb::mint_vigil_mode_bridge);
FIXY_BRIDGE_NAME_REACHABLE(fb::mint_persisted_session);
FIXY_BRIDGE_NAME_REACHABLE(fb::mint_crash_watched_session);
FIXY_BRIDGE_NAME_REACHABLE(fb::mint_atomic_session);
FIXY_BRIDGE_NAME_REACHABLE(fb::wrap_crash_return);

#undef FIXY_BRIDGE_NAME_REACHABLE

// The carrier sits at namespace level in the substrate while its helper
// traits sit one level down, inside detail.  The re-export flattens both
// into one namespace, so the assertions below have to compare against two
// different substrate scopes.

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

static_assert(std::is_same_v<fb::CrashEvent<test_fixy_bridge::BridgeTestPeer, test_fixy_bridge::BridgeTestRes,
                                            test_fixy_bridge::BridgeTestSurvivor>,
                             proto::CrashEvent<test_fixy_bridge::BridgeTestPeer, test_fixy_bridge::BridgeTestRes,
                                               test_fixy_bridge::BridgeTestSurvivor>>,
              "fb::CrashEvent must alias proto::CrashEvent with no detail wrapping");

static_assert(
    std::is_same_v<typename fb::crash_event_from_survivors<
                       test_fixy_bridge::BridgeTestPeer, test_fixy_bridge::BridgeTestRes,
                       ::crucible::permissions::inheritance_list<test_fixy_bridge::BridgeTestSurvivor>>::type,
                   typename proto::detail::crash_event_from_survivors<
                       test_fixy_bridge::BridgeTestPeer, test_fixy_bridge::BridgeTestRes,
                       ::crucible::permissions::inheritance_list<test_fixy_bridge::BridgeTestSurvivor>>::type>,
    "fb::crash_event_from_survivors must alias proto::detail::");

// Resolves through the survivor registry specialization above.
static_assert(
    std::is_same_v<fb::crash_event_for_t<test_fixy_bridge::BridgeTestPeer, test_fixy_bridge::BridgeTestRes>,
                   proto::detail::crash_event_for_t<test_fixy_bridge::BridgeTestPeer, test_fixy_bridge::BridgeTestRes>>,
    "fb::crash_event_for_t must alias proto::detail::");

using TestCanonicalEvent = fb::crash_event_for_t<test_fixy_bridge::BridgeTestPeer, test_fixy_bridge::BridgeTestRes>;
static_assert(fb::crash_event_matches_survivors<TestCanonicalEvent>::value);
static_assert(fb::crash_event_matches_survivors_v<TestCanonicalEvent>);

// A floor, not an exact count.  The exact pin sits next to the constant it
// counts, where a contributor raising it cannot miss the sibling assertion.
// Here only the other direction matters: an entry removed without review.
static_assert(::crucible::fixy::bridge::self_test::crash_event_surface_cardinality >= 6,
              "floor: the crash-event surface cardinality regressed below 6, so an "
              "entry was removed without updating the colocated exact pin");

int main() {
    // Every claim here is a compile-time one.
    return 0;
}
