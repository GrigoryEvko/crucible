// Each live role of G has an entry (clause A1 of Definition 4.19).  Q
// acts in G, and the context has no entry for Q.

#include <fixy/session/CrashAssociation.h>

// These types have external linkage.  The session folds their stable
// ids, and a stable id refuses a type with internal linkage.
namespace neg_sess_crash_assoc_live_role_missing_types {

namespace g = ::fixy::session::global;
namespace s = ::fixy::session;
namespace c = ::fixy::session::config;

struct P {};
struct Q {};
struct M {};

using Guarded = g::Comm<P, Q, g::Branch<M, int, g::End>, g::Branch<g::CrashLabel, void, g::End>>;
using OnlyQ = s::ReliableSet<Q>;
using NoQ = s::TypingContext<s::RoleState<P, s::OutQueue<>, s::Send<s::PeerMsg<Q, M, int>, s::End>>>;

constexpr int check_context() noexcept {
    c::ensure_crash_associated<NoQ, g::State<g::Roles<>, Guarded>, OnlyQ>();
    return 0;
}

}  // namespace neg_sess_crash_assoc_live_role_missing_types

using namespace neg_sess_crash_assoc_live_role_missing_types;

int main() { return check_context(); }
