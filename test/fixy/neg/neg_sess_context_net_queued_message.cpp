// P holds a message to Q in its queue.  The marked system reads only a
// context whose queues are empty, so the context is outside the model of the
// check, and the door refuses it.

#include <fixy/session/ContextNet.h>

#include <cstddef>

// These types have external linkage.  The session folds their stable ids,
// and a stable id refuses a type with internal linkage.
namespace neg_sess_context_net_queued_message_types {

namespace s = ::fixy::session;

struct P {};
struct Q {};
struct X {};

struct OneSlot {
    static constexpr std::size_t channel_capacity = 1;
};

using WithQueue = s::TypingContext<s::RoleState<P, s::OutQueue<s::Queued<Q, X, int>>, s::End>,
                                   s::RoleState<Q, s::OutQueue<>, s::Recv<s::PeerMsg<P, X, int>, s::End>>>;

constexpr int require_deadlock_free() noexcept {
    s::ensure_context_deadlock_free<WithQueue, OneSlot>();
    return 0;
}

}  // namespace neg_sess_context_net_queued_message_types

using namespace neg_sess_context_net_queued_message_types;

int main() { return require_deadlock_free(); }
