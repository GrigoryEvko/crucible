// A role that is neither live in G nor crashed is at End (clause A3 of
// Definition 4.19).  G has ended, and the entry of P still waits for a
// message.

#include <fixy/session/CrashAssociation.h>

// These types have external linkage.  The session folds their stable
// ids, and a stable id refuses a type with internal linkage.
namespace neg_sess_crash_assoc_role_after_end_types {

namespace g = ::fixy::session::global;
namespace s = ::fixy::session;
namespace c = ::fixy::session::config;

struct P {};
struct Q {};
struct M {};

using Waits = s::TypingContext<s::RoleState<P, s::OutQueue<>, s::Recv<s::PeerMsg<Q, M, int>, s::End>>>;

constexpr int check_context() noexcept {
    c::ensure_crash_associated<Waits, g::State<g::Roles<>, g::End>, s::NoReliableRoles>();
    return 0;
}

}  // namespace neg_sess_crash_assoc_role_after_end_types

using namespace neg_sess_crash_assoc_role_after_end_types;

int main() { return check_context(); }
