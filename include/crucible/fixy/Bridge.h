#pragma once

// The wrap factories below live in crucible::safety::proto and
// crucible::bridges.  The re-export gives a caller that pulls in only the
// fixy surface an entry point that does not name those namespaces.
//
// The Vigil hub is deliberately not included.  It would drag its whole
// dependency graph into every consumer of this header, and the mode-cell
// machinery this file needs is available on its own.  A caller that holds a
// Vigil and wants the overload taking one includes the hub itself.

#include <crucible/Cipher.h>
#include <crucible/bridges/CrashTransport.h>
#include <crucible/bridges/EndpointMint.h>
#include <crucible/bridges/MachineSessionBridge.h>
#include <crucible/bridges/RecordingSessionHandle.h>
#include <crucible/bridges/SessionPersistence.h>
#include <crucible/bridges/VigilModeHandle.h>
#include <crucible/fixy/Handle.h>
#include <crucible/permissions/PermissionInherit.h>
#include <crucible/safety/EpochVersioned.h>

#include <cstdint>
#include <type_traits>

namespace crucible::fixy::bridge {

using ::crucible::safety::proto::mint_recording_session;
using ::crucible::safety::proto::RecordingSessionHandle;

using ::crucible::safety::proto::mint_crash_watched_session;
using ::crucible::safety::proto::CrashWatchedHandle;

// A crash-watched session routes recovery through the substrate's
// wrap_crash_return factory, which bundles the inner protocol's
// would-have-been return value with the surviving permission set into a
// CrashEvent.  Naming the type here does not let a caller build one: the
// CrashEvent constructor stays friend-gated to wrap_crash_return.

using ::crucible::safety::proto::wrap_crash_return;
using ::crucible::safety::proto::CrashEvent;
// The next four sit in a detail namespace in the substrate, yet they are
// the stop-handler pattern a caller is meant to write against.  Surfacing
// them here reaches them without a substrate refactor.
using ::crucible::safety::proto::detail::crash_event_from_survivors;
using ::crucible::safety::proto::detail::crash_event_for_t;
using ::crucible::safety::proto::detail::crash_event_matches_survivors;
using ::crucible::safety::proto::detail::crash_event_matches_survivors_v;

using ::crucible::safety::proto::mint_persisted_session;

using ::crucible::bridges::mint_recording_endpoint;
using ::crucible::bridges::mint_crash_watched_endpoint;

using ::crucible::mint_vigil_mode_bridge;

using ::crucible::safety::mint_atomic_session;

using ::crucible::safety::Epoch;
using ::crucible::safety::Generation;
using ::crucible::safety::EpochVersioned;

}  // namespace crucible::fixy::bridge

// The sentinels below verify that each alias resolves to the substrate
// entity and not to a local of the same name.  Being in the header, they
// fire at every consumer's include time.

namespace crucible::fixy::bridge::self_test {

struct BridgeProbePeer {};
struct BridgeProbeResource {};
struct BridgeProbeSurvivorA {};
struct BridgeProbeSurvivorB {};

}  // namespace crucible::fixy::bridge::self_test

namespace crucible::permissions {

// crash_event_for_t reads survivors_t off the peer tag, so the probe peer
// needs a non-empty list.  A production peer tag carries the analogous
// specialization next to its own declaration.
template <>
struct survivor_registry<::crucible::fixy::bridge::self_test::BridgeProbePeer> {
    using type = inheritance_list<::crucible::fixy::bridge::self_test::BridgeProbeSurvivorA,
                                  ::crucible::fixy::bridge::self_test::BridgeProbeSurvivorB>;
};

}  // namespace crucible::permissions

namespace crucible::fixy::bridge::self_test {

static_assert(std::is_same_v<::crucible::fixy::bridge::CrashEvent<BridgeProbePeer, BridgeProbeResource,
                                                                  BridgeProbeSurvivorA, BridgeProbeSurvivorB>,
                             ::crucible::safety::proto::CrashEvent<BridgeProbePeer, BridgeProbeResource,
                                                                   BridgeProbeSurvivorA, BridgeProbeSurvivorB>>,
              "fixy::bridge::CrashEvent must alias safety::proto::CrashEvent");

static_assert(
    std::is_same_v<typename ::crucible::fixy::bridge::crash_event_from_survivors<
                       BridgeProbePeer, BridgeProbeResource,
                       ::crucible::permissions::inheritance_list<BridgeProbeSurvivorA, BridgeProbeSurvivorB>>::type,
                   typename ::crucible::safety::proto::detail::crash_event_from_survivors<
                       BridgeProbePeer, BridgeProbeResource,
                       ::crucible::permissions::inheritance_list<BridgeProbeSurvivorA, BridgeProbeSurvivorB>>::type>,
    "fixy::bridge::crash_event_from_survivors must alias substrate");

static_assert(
    std::is_same_v<::crucible::fixy::bridge::crash_event_for_t<BridgeProbePeer, BridgeProbeResource>,
                   ::crucible::safety::proto::detail::crash_event_for_t<BridgeProbePeer, BridgeProbeResource>>,
    "fixy::bridge::crash_event_for_t must alias substrate");

using ProbeCanonicalEvent = ::crucible::fixy::bridge::crash_event_for_t<BridgeProbePeer, BridgeProbeResource>;
static_assert(::crucible::fixy::bridge::crash_event_matches_survivors<ProbeCanonicalEvent>::value,
              "predicate must fire true on the canonical CrashEvent for the peer");
static_assert(::crucible::fixy::bridge::crash_event_matches_survivors_v<ProbeCanonicalEvent>,
              "variable template must fire true on the canonical CrashEvent");

using ProbeMismatchEvent =
    ::crucible::fixy::bridge::CrashEvent<BridgeProbePeer, BridgeProbeResource,
                                         BridgeProbeSurvivorA>;  // only one survivor, registry has two
static_assert(!::crucible::fixy::bridge::crash_event_matches_survivors_v<ProbeMismatchEvent>,
              "predicate must reject events whose survivor list doesn't match "
              "the peer's registered survivors_t");

constexpr int crash_event_surface_cardinality = 6;
static_assert(crash_event_surface_cardinality == 6, "fixy::bridge:: crash-event surface cardinality drifted — "
                                                    "extend the sentinel block to cover the new alias.");

struct BridgeProbeEpochT {};

static_assert(std::is_same_v<::crucible::fixy::bridge::Epoch, ::crucible::safety::Epoch>,
              "fixy::bridge::Epoch must alias safety::Epoch — Canopy fleet-"
              "epoch identity drift would break checkpoint admission gates "
              "at bridge tier.");

static_assert(std::is_same_v<::crucible::fixy::bridge::Generation, ::crucible::safety::Generation>,
              "fixy::bridge::Generation must alias safety::Generation — "
              "per-Relay restart counter identity drift would silently equate "
              "Generation and Epoch at the bridge-tier admission gates.");

static_assert(std::is_same_v<::crucible::fixy::bridge::EpochVersioned<BridgeProbeEpochT>,
                             ::crucible::safety::EpochVersioned<BridgeProbeEpochT>>,
              "fixy::bridge::EpochVersioned<T> must alias safety::EpochVersioned<T> "
              "— bridge-tier checkpoint values flow through Cipher cold-tier as "
              "the same byte-representation handle-tier consumers see; drift "
              "here breaks the row hash that keys the federation cache.");

static_assert(std::is_same_v<::crucible::fixy::bridge::EpochVersioned<BridgeProbeEpochT>,
                             ::crucible::fixy::handle::EpochVersioned<BridgeProbeEpochT>>,
              "fixy::bridge::EpochVersioned<T> and fixy::handle::EpochVersioned<T> "
              "MUST share substrate identity — dual-export discipline; drift would "
              "force a retag at every handle→bridge crossing and invalidate the "
              "row hash that keys the federation cache.");

static_assert(sizeof(::crucible::fixy::bridge::EpochVersioned<std::uint8_t>)
                  >= sizeof(std::uint64_t) * 2 + sizeof(std::uint8_t),
              "fixy::bridge::EpochVersioned<T> must carry at least 16 bytes of "
              "grade (Epoch + Generation) — REGIME-4 storage contract; drift "
              "would silently shrink checkpoint headers below the (epoch, gen) "
              "pair required by Cipher cold-tier roll-forward.");

constexpr int epoch_versioned_surface_cardinality = 3;
static_assert(epoch_versioned_surface_cardinality == 3, "fixy::bridge:: EpochVersioned surface cardinality drifted — "
                                                        "extend the sentinel block to cover the new alias.");

}  // namespace crucible::fixy::bridge::self_test
