#pragma once

// The recording wrapper is a free function rather than an endpoint method
// so that the endpoint header does not have to include the recording
// header.  As a method it would pull the whole recording machinery into
// every endpoint user.  As a free function in a dedicated header the
// include cost is opt-in.
//
// No mint wraps an endpoint in a crash watch.  The crash session
// (fixy/session/CrashTransport.h) admits only a protocol in which every
// receive has a branch for the crash of the peer.  The default protocol of
// an endpoint has no such branch.

#include <fixy/concurrent/Endpoint.h>
#include <fixy/session/EventLog.h>
#include <fixy/session/Recording.h>

#include <foundation/effects/Ctx.h>

#include <type_traits>
#include <utility>

namespace fixy::concurrent {

// The log is held by reference rather than owned, so the two sides of one
// channel can share a log and produce a single ordered record of the
// exchange.  The requires-clause restates what the parameter type already
// asks, so that the gate of this mint is one named concept.

template <class Substr, Direction Dir, ::foundation::effects::IsExecCtx Ctx>
    requires IsBridgeableDirection<Substr, Dir>
[[nodiscard]] constexpr auto mint_recording_endpoint(Endpoint<Substr, Dir, Ctx>&& ep,
                                                     ::fixy::session::SessionEventLog& log,
                                                     ::fixy::session::RoleTagId self_role,
                                                     ::fixy::session::RoleTagId peer_role) noexcept {
    return ::fixy::session::mint_recorded_session(std::move(ep).into_bare_session(), log, self_role, peer_role);
}

}  // namespace fixy::concurrent
