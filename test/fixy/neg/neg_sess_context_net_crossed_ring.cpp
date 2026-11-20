// Three roles play three live sessions in a ring, and each role waits on its
// left neighbour first.  Each session alone is live, and the context
// deadlocks.  The marked system of the context holds a cycle of waits with no
// token, so the door refuses it.

#include <fixy/session/ContextNet.h>

#include <cstddef>

// These types have external linkage.  The session folds their stable ids,
// and a stable id refuses a type with internal linkage.
namespace neg_sess_context_net_crossed_ring_types {

namespace g = ::fixy::session::global;
namespace s = ::fixy::session;

struct P {};
struct Q {};
struct R {};
struct X {};

struct OneSlot {
    static constexpr std::size_t channel_capacity = 1;
};

using SessionPQ = g::Msg<P, Q, X, int, g::End>;
using SessionQR = g::Msg<Q, R, X, int, g::End>;
using SessionRP = g::Msg<R, P, X, int, g::End>;
using RingP = s::compose_t<typename s::project_t<SessionRP, P>::local, typename s::project_t<SessionPQ, P>::local>;
using RingQ = s::compose_t<typename s::project_t<SessionPQ, Q>::local, typename s::project_t<SessionQR, Q>::local>;
using RingR = s::compose_t<typename s::project_t<SessionQR, R>::local, typename s::project_t<SessionRP, R>::local>;
using CrossedRing = s::TypingContext<s::RoleState<P, s::OutQueue<>, RingP>, s::RoleState<Q, s::OutQueue<>, RingQ>,
                                     s::RoleState<R, s::OutQueue<>, RingR>>;

constexpr int require_deadlock_free() noexcept {
    s::ensure_context_deadlock_free<CrossedRing, OneSlot>();
    return 0;
}

}  // namespace neg_sess_context_net_crossed_ring_types

using namespace neg_sess_context_net_crossed_ring_types;

int main() { return require_deadlock_free(); }
