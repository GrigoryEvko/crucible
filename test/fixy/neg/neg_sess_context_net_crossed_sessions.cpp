// Two roles play two live sessions and wait on them in opposite orders: P
// waits on session two first, and Q waits on session one first.  Each
// session alone is live, and the context deadlocks.  The marked system of the
// context holds a cycle of waits with no token, so the door refuses it.

#include <fixy/session/ContextNet.h>

#include <cstddef>

// These types have external linkage.  The session folds their stable ids,
// and a stable id refuses a type with internal linkage.
namespace neg_sess_context_net_crossed_sessions_types {

namespace g = ::fixy::session::global;
namespace s = ::fixy::session;

struct P {};
struct Q {};
struct X {};
struct Y {};

struct OneSlot {
    static constexpr std::size_t channel_capacity = 1;
};

using SessionOne = g::Msg<P, Q, X, int, g::End>;
using SessionTwo = g::Msg<Q, P, Y, int, g::End>;
using ProcessP = s::compose_t<typename s::project_t<SessionTwo, P>::local, typename s::project_t<SessionOne, P>::local>;
using ProcessQ = s::compose_t<typename s::project_t<SessionOne, Q>::local, typename s::project_t<SessionTwo, Q>::local>;
using CrossedSessions =
    s::TypingContext<s::RoleState<P, s::OutQueue<>, ProcessP>, s::RoleState<Q, s::OutQueue<>, ProcessQ>>;

constexpr int require_deadlock_free() noexcept {
    s::ensure_context_deadlock_free<CrossedSessions, OneSlot>();
    return 0;
}

}  // namespace neg_sess_context_net_crossed_sessions_types

using namespace neg_sess_context_net_crossed_sessions_types;

int main() { return require_deadlock_free(); }
