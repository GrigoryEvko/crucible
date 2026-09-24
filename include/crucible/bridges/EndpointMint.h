#pragma once

// These wrappers are free functions rather than endpoint methods so that the
// endpoint header does not have to include the bridge headers.  As methods
// they would pull the whole bridge machinery into every endpoint user.  As
// free functions in a dedicated header the include cost is opt-in.

#include <crucible/Platform.h>
#include <crucible/bridges/CrashTransport.h>
#include <crucible/bridges/RecordingSessionHandle.h>
#include <crucible/concurrent/Endpoint.h>
#include <crucible/handles/OneShotFlag.h>
#include <crucible/sessions/SessionEventLog.h>

#include <type_traits>
#include <utility>

namespace crucible::bridges {

// The log is held by reference rather than owned, so the two sides of one
// channel can share a log and produce a single ordered record of the exchange.

template <class Substr, ::crucible::concurrent::Direction Dir, ::crucible::effects::IsExecCtx Ctx>
    requires ::crucible::concurrent::IsBridgeableDirection<Substr, Dir>
[[nodiscard]] constexpr auto mint_recording_endpoint(::crucible::concurrent::Endpoint<Substr, Dir, Ctx>&& ep,
                                                     ::crucible::safety::proto::SessionEventLog& log,
                                                     ::crucible::safety::proto::RoleTagId self_role,
                                                     ::crucible::safety::proto::RoleTagId peer_role) noexcept {
    return ::crucible::safety::proto::mint_recording_session(std::move(ep).into_bare_session(), log, self_role,
                                                             peer_role);
}

// PeerTag names the peer being watched.  It is explicit rather than deduced
// because one handle can be watched against different peers.

template <class PeerTag, class Substr, ::crucible::concurrent::Direction Dir, ::crucible::effects::IsExecCtx Ctx>
    requires ::crucible::concurrent::IsBridgeableDirection<Substr, Dir>
[[nodiscard]] constexpr auto mint_crash_watched_endpoint(::crucible::concurrent::Endpoint<Substr, Dir, Ctx>&& ep,
                                                         ::crucible::safety::OneShotFlag& flag) noexcept {
    return ::crucible::safety::proto::mint_crash_watched_session<PeerTag>(std::move(ep).into_bare_session(), flag);
}

namespace detail::endpoint_mint_self_test {

namespace eff = ::crucible::effects;
namespace conc = ::crucible::concurrent;
namespace proto = ::crucible::safety::proto;

struct UserTag {};
using SmallSpsc = conc::PermissionedSpscChannel<int, 64, UserTag>;
using ProdEp = conc::Endpoint<SmallSpsc, conc::Direction::Producer, eff::HotFgCtx>;
using ConsEp = conc::Endpoint<SmallSpsc, conc::Direction::Consumer, eff::BgDrainCtx>;

using ProdProto = proto::Loop<proto::Send<int, proto::Continue>>;
using ConsProto = proto::Loop<proto::Recv<int, proto::Continue>>;

// A loop is unrolled before the handle is typed, so the wrapper carries the
// head of the loop body, not the loop itself.
using ProdHead = proto::Send<int, proto::Continue>;
using ConsHead = proto::Recv<int, proto::Continue>;

using ExpectedRecording = proto::RecordingSessionHandle<ProdHead, typename SmallSpsc::ProducerHandle*, ProdProto>;

struct PeerA {};
using ExpectedCrashWatched = proto::CrashWatchedHandle<ProdHead, typename SmallSpsc::ProducerHandle*, PeerA,
                                                       proto::CrashClass::Abort, ProdProto>;

}  // namespace detail::endpoint_mint_self_test

}  // namespace crucible::bridges
