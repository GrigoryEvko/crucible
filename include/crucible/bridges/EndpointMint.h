#pragma once

// ── crucible::bridges::mint_recording_endpoint / mint_crash_watched_endpoint ──
//
// The two missing Endpoint views (3 + 4 of the 4 promised in
// concurrent/Endpoint.h's docstring): RecordingSessionHandle and
// CrashWatchedHandle wrappers around the Endpoint's bare session
// view.
//
// Per the Universal Mint Pattern (CLAUDE.md §XXI):
//
//   mint_recording_endpoint(ep, log, self, peer)  → RecordingSessionHandle
//   mint_crash_watched_endpoint<PeerTag>(ep, flag) → CrashWatchedHandle
//
// Both factories CONSUME the Endpoint by `&&`, extract its bare
// session view via `.into_bare_session()`, then wrap it in the
// corresponding bridge.  The Endpoint's typed view is consumed; the
// underlying handle's Permission discipline still enforces single-
// {producer,consumer,...} linearity at the channel layer.
//
//   Axiom coverage: TypeSafe — bridge wrapper inherits the
//                   SessionHandle's protocol typing; bare session
//                   extraction preserves the Proto + Resource +
//                   LoopCtx tuple.
//                   InitSafe — both wrappers ship via existing
//                   factories (mint_recording_session,
//                   mint_crash_watched_session).
//                   MemSafe — Endpoint's && consumes the source;
//                   the resulting bridge wrapper owns the typed view.
//                   BorrowSafe — single-owner discipline preserved
//                   across the bridge.
//   Runtime cost:   zero.  Each factory inlines through the existing
//                   bridge constructor; the Endpoint extraction is
//                   a single pointer copy.
//
// ── Usage pattern ───────────────────────────────────────────────────
//
//   auto ep = mint_endpoint<Channel, Direction::Producer>(ctx, h);
//
//   // Pick ONE of the four views:
//
//   ep.try_send(value);                                // raw view
//   auto sess = std::move(ep).into_session();           // PSH session view
//   auto rec  = mint_recording_endpoint(                // recording view
//       std::move(ep), log, self_id, peer_id);
//   auto cw   = mint_crash_watched_endpoint<PeerTag>(   // crash-watched view
//       std::move(ep), peer_flag);
//
// All four views consume the Endpoint and produce a typed handle
// the user drives forward.  After the consumption, the Endpoint is
// moved-from (handle_ nulled per AUDIT-5); using it would deref
// nullptr.
//
// ── Why free functions, not methods ────────────────────────────────
//
// Endpoint's into_recording_session / into_crash_watched as METHODS
// would force concurrent/Endpoint.h to include bridges/Recording*.h
// and bridges/CrashTransport.h — pulling 1300+ LOC of bridge
// machinery into every Endpoint user.  Free functions in this
// dedicated header keep the include cost opt-in: callers who don't
// need the bridge views never see the bridge headers.
//
// ── References ──────────────────────────────────────────────────────
//
//   concurrent/Endpoint.h           — the source typed view
//   bridges/RecordingSessionHandle.h — RecordingSessionHandle wrapper
//   bridges/CrashTransport.h         — CrashWatchedHandle wrapper
//   CLAUDE.md §XXI                   — Universal Mint Pattern
//   CLAUDE.md §XVIII HS14            — neg-compile fixture requirement

#include <crucible/Platform.h>
#include <crucible/bridges/CrashTransport.h>
#include <crucible/bridges/RecordingSessionHandle.h>
#include <crucible/concurrent/Endpoint.h>
#include <crucible/handles/OneShotFlag.h>
#include <crucible/sessions/SessionEventLog.h>

#include <type_traits>
#include <utility>

namespace crucible::bridges {

// ── mint_recording_endpoint(ep, log, self, peer) ───────────────────
//
// Wraps ep's bare session view in a RecordingSessionHandle that
// records every send / recv / close to the supplied SessionEventLog.
// Returns the standard RecordingSessionHandle from
// bridges/RecordingSessionHandle.h.
//
// The log lives outside the wrapper (passed by reference); a single
// log can capture multiple Endpoints' sessions (Sender's and
// Receiver's both writing to the same log produces a unified audit
// trail with monotonic step_ids across both sides).

template <class Substr,
          ::crucible::concurrent::Direction Dir,
          ::crucible::effects::IsExecCtx Ctx>
[[nodiscard]] constexpr auto mint_recording_endpoint(
    ::crucible::concurrent::Endpoint<Substr, Dir, Ctx>&& ep,
    ::crucible::safety::proto::SessionEventLog& log,
    ::crucible::safety::proto::RoleTagId self_role,
    ::crucible::safety::proto::RoleTagId peer_role) noexcept
{
    return ::crucible::safety::proto::mint_recording_session(
        std::move(ep).into_bare_session(),
        log,
        self_role,
        peer_role);
}

// ── mint_crash_watched_endpoint<PeerTag>(ep, flag) ─────────────────
//
// Wraps ep's bare session view in a CrashWatchedHandle that consults
// the supplied OneShotFlag before each send / recv.  PeerTag is the
// reliability label (BSYZ22 crash-stop) for the peer being watched;
// it must be specified explicitly because the same handle could be
// watched against different peers.
//
// Returns the standard CrashWatchedHandle from bridges/CrashTransport.h.

template <class PeerTag,
          class Substr,
          ::crucible::concurrent::Direction Dir,
          ::crucible::effects::IsExecCtx Ctx>
[[nodiscard]] constexpr auto mint_crash_watched_endpoint(
    ::crucible::concurrent::Endpoint<Substr, Dir, Ctx>&& ep,
    ::crucible::safety::OneShotFlag& flag) noexcept
{
    return ::crucible::safety::proto::mint_crash_watched_session<PeerTag>(
        std::move(ep).into_bare_session(),
        flag);
}

// ── Self-test block ─────────────────────────────────────────────────
namespace detail::endpoint_mint_self_test {

namespace eff   = ::crucible::effects;
namespace conc  = ::crucible::concurrent;
namespace proto = ::crucible::safety::proto;

struct UserTag {};
using SmallSpsc = conc::PermissionedSpscChannel<int, 64, UserTag>;
using ProdEp    = conc::Endpoint<SmallSpsc, conc::Direction::Producer, eff::HotFgCtx>;
using ConsEp    = conc::Endpoint<SmallSpsc, conc::Direction::Consumer, eff::BgDrainCtx>;

// ── Type-level pinning of the bridge view return types ─────────────
//
// The bridge factories must return the standard bridge wrapper types
// from bridges/RecordingSessionHandle.h and bridges/CrashTransport.h.
// Pin the return type at type-instantiation so changes to the
// underlying bridge constructors surface here.

using ProdProto = proto::Loop<proto::Send<int, proto::Continue>>;
using ConsProto = proto::Loop<proto::Recv<int, proto::Continue>>;

// After Loop unrolling the head type is Send<int, Continue> /
// Recv<int, Continue> respectively.
using ProdHead = proto::Send<int, proto::Continue>;
using ConsHead = proto::Recv<int, proto::Continue>;

// RecordingSessionHandle parametrized over the same Proto + bare
// SessionHandle Resource (Handle*).
using ExpectedRecording =
    proto::RecordingSessionHandle<ProdHead,
                                   typename SmallSpsc::ProducerHandle*,
                                   ProdProto>;

// CrashWatchedHandle parametrized similarly with a PeerTag.
struct PeerA {};
using ExpectedCrashWatched =
    proto::CrashWatchedHandle<ProdHead,
                               typename SmallSpsc::ProducerHandle*,
                               PeerA,
                               ProdProto>;

}  // namespace detail::endpoint_mint_self_test

// ── Runtime smoke test ──────────────────────────────────────────────
//
// TYPE-LEVEL ONLY.  We don't actually mint the wrapper instances:
// the underlying protocol is `Loop<Send<T, Continue>>` (infinite, no
// End/Stop), and the bridge wrappers don't expose the bare-handle's
// `.detach(reason_tag)` infinite-loop escape hatch — the wrapper's
// .detach() marks the wrapper consumed but leaves the inner
// SessionHandle un-consumed, firing its destructor's abandonment
// abort.  Driving the wrapper to clean destruction would require a
// real producer/consumer thread pair with a transport callback, out
// of scope for a header-internal smoke.
//
// This test verifies the factories COMPILE, the type-level
// resolution (Endpoint × log/flag) → wrapper type is well-formed,
// and the Universal Mint Pattern signature is honored.  The full
// end-to-end exercise lives in the production wire-in tests once
// they land.

[[gnu::cold]] inline void runtime_smoke_test_endpoint_mint() noexcept {
    namespace eff   = ::crucible::effects;
    namespace conc  = ::crucible::concurrent;
    namespace proto = ::crucible::safety::proto;
    namespace saf   = ::crucible::safety;

    struct SmokeTag {};
    using Channel = conc::PermissionedSpscChannel<int, 64, SmokeTag>;
    using ProdEpT = conc::Endpoint<Channel, conc::Direction::Producer, eff::HotFgCtx>;
    using ConsEpT = conc::Endpoint<Channel, conc::Direction::Consumer, eff::BgDrainCtx>;

    using ProdLoop = proto::Loop<proto::Send<int, proto::Continue>>;
    using ConsLoop = proto::Loop<proto::Recv<int, proto::Continue>>;
    using ProdHead = proto::Send<int, proto::Continue>;
    using ConsHead = proto::Recv<int, proto::Continue>;

    using ExpectedRec =
        proto::RecordingSessionHandle<ProdHead,
                                       typename Channel::ProducerHandle*,
                                       ProdLoop>;

    struct PeerB {};
    using ExpectedCW =
        proto::CrashWatchedHandle<ConsHead,
                                   typename Channel::ConsumerHandle*,
                                   PeerB,
                                   ConsLoop>;

    static_assert(std::is_same_v<
        decltype(mint_recording_endpoint(
            std::declval<ProdEpT&&>(),
            std::declval<proto::SessionEventLog&>(),
            proto::RoleTagId{1}, proto::RoleTagId{2})),
        ExpectedRec>,
        "mint_recording_endpoint must return RecordingSessionHandle "
        "typed over Endpoint's protocol head + Resource");

    static_assert(std::is_same_v<
        decltype(mint_crash_watched_endpoint<PeerB>(
            std::declval<ConsEpT&&>(),
            std::declval<saf::OneShotFlag&>())),
        ExpectedCW>,
        "mint_crash_watched_endpoint must return CrashWatchedHandle "
        "typed over Endpoint's protocol head + Resource + PeerTag");
}

}  // namespace crucible::bridges
