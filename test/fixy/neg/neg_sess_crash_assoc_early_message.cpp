// The queues hold what G has en route (clause A4 of Definition 4.19).
// Under p → q the queue from P to Q is empty, and here it already holds
// the message that P has not sent.

#include <fixy/session/CrashAssociation.h>

// These types have external linkage.  The session folds their stable
// ids, and a stable id refuses a type with internal linkage.
namespace neg_sess_crash_assoc_early_message_types {

namespace g = ::fixy::session::global;
namespace s = ::fixy::session;
namespace c = ::fixy::session::config;

struct P {};
struct Q {};
struct M {};

using Guarded = g::Comm<P, Q, g::Branch<M, int, g::End>, g::Branch<g::CrashLabel, void, g::End>>;
using OnlyQ = s::ReliableSet<Q>;
using Early =
    s::TypingContext<s::RoleState<P, s::OutQueue<s::Queued<Q, M, int>>, s::Send<s::PeerMsg<Q, M, int>, s::End>>,
                     s::RoleState<Q, s::OutQueue<>,
                                  s::Offer<s::Sender<P>, s::Recv<s::PeerMsg<P, M, int>, s::End>,
                                           s::Recv<s::PeerMsg<P, g::CrashLabel, void>, s::End>>>>;

constexpr int check_context() noexcept {
    c::ensure_crash_associated<Early, g::State<g::Roles<>, Guarded>, OnlyQ>();
    return 0;
}

}  // namespace neg_sess_crash_assoc_early_message_types

using namespace neg_sess_crash_assoc_early_message_types;

int main() { return check_context(); }
