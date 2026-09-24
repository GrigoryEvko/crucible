// The queue to a crashed role is unavailable (clause A4 (i) of
// Definition 4.19): the crash drops each message to it, and a later send
// is dropped too.  Q crashed, and the queue of P still holds a message
// to Q.

#include <fixy/session/CrashAssociation.h>

// These types have external linkage.  The session folds their stable
// ids, and a stable id refuses a type with internal linkage.
namespace neg_sess_crash_assoc_message_to_crashed_types {

namespace g = ::fixy::session::global;
namespace s = ::fixy::session;
namespace c = ::fixy::session::config;

struct P {};
struct Q {};
struct M {};

using Guarded = g::Comm<P, Q, g::Branch<M, int, g::End>, g::Branch<g::CrashLabel, void, g::End>>;
using QCrashed = g::state_step_t<g::State<g::Roles<>, Guarded>, g::CrashAction<Q>, s::NoReliableRoles>;
using ToCrashed = s::TypingContext<
    s::RoleState<P, s::OutQueue<s::Queued<Q, M, int>>, s::Send<s::PeerMsg<Q, M, int>, s::End>>,
    s::RoleState<Q, s::OutQueue<>, s::Stop>>;

constexpr int check_context() noexcept {
    c::ensure_crash_associated<ToCrashed, QCrashed, s::NoReliableRoles>();
    return 0;
}

}  // namespace neg_sess_crash_assoc_message_to_crashed_types

using namespace neg_sess_crash_assoc_message_to_crashed_types;

int main() { return check_context(); }
