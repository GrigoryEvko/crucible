// A role that is neither live in G nor crashed is at End (clause A3 of
// Definition 4.19).  R takes no part in G, and its entry still sends.

#include <fixy/session/CrashAssociation.h>

// These types have external linkage.  The session folds their stable
// ids, and a stable id refuses a type with internal linkage.
namespace neg_sess_crash_assoc_stray_role_types {

namespace g = ::fixy::session::global;
namespace s = ::fixy::session;
namespace c = ::fixy::session::config;

struct P {};
struct Q {};
struct R {};
struct M {};

using Guarded = g::Comm<P, Q, g::Branch<M, int, g::End>, g::Branch<g::CrashLabel, void, g::End>>;
using OnlyQ = s::ReliableSet<Q>;
using Stray = s::TypingContext<
    s::RoleState<P, s::OutQueue<>, s::Send<s::PeerMsg<Q, M, int>, s::End>>,
    s::RoleState<Q, s::OutQueue<>,
                 s::Offer<s::Sender<P>, s::Recv<s::PeerMsg<P, M, int>, s::End>,
                          s::Recv<s::PeerMsg<P, g::CrashLabel, void>, s::End>>>,
    s::RoleState<R, s::OutQueue<>, s::Send<s::PeerMsg<P, M, int>, s::End>>>;

constexpr int check_context() noexcept {
    c::ensure_crash_associated<Stray, g::State<g::Roles<>, Guarded>, OnlyQ>();
    return 0;
}

}  // namespace neg_sess_crash_assoc_stray_role_types

using namespace neg_sess_crash_assoc_stray_role_types;

int main() { return check_context(); }
