// P sends to R, and the context holds no role R.  The check cannot name the
// channel of that message, so the context is outside the model of the check,
// and the door refuses it.

#include <fixy/session/ContextNet.h>

#include <cstddef>

// These types have external linkage.  The session folds their stable ids,
// and a stable id refuses a type with internal linkage.
namespace neg_sess_context_net_peer_outside_types {

namespace s = ::fixy::session;

struct P {};
struct Q {};
struct R {};
struct X {};

struct OneSlot {
    static constexpr std::size_t channel_capacity = 1;
};

using ToStranger = s::TypingContext<s::RoleState<P, s::OutQueue<>, s::Send<s::PeerMsg<R, X, int>, s::End>>,
                                    s::RoleState<Q, s::OutQueue<>, s::End>>;

constexpr int require_deadlock_free() noexcept {
    s::ensure_context_deadlock_free<ToStranger, OneSlot>();
    return 0;
}

}  // namespace neg_sess_context_net_peer_outside_types

using namespace neg_sess_context_net_peer_outside_types;

int main() { return require_deadlock_free(); }
