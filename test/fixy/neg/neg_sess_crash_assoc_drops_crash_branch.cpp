// A live role refines its crash-stop projection (clause A1 of Definition
// 4.19).  P is not reliable, so the projection onto Q has a crash
// branch, and rule Sub-& lets no entry drop it.

#include <fixy/session/CrashAssociation.h>

// These types have external linkage.  The session folds their stable
// ids, and a stable id refuses a type with internal linkage.
namespace neg_sess_crash_assoc_drops_crash_branch_types {

namespace g = ::fixy::session::global;
namespace s = ::fixy::session;
namespace c = ::fixy::session::config;

struct P {};
struct Q {};
struct M {};

using Guarded = g::Comm<P, Q, g::Branch<M, int, g::End>, g::Branch<g::CrashLabel, void, g::End>>;
using OnlyQ = s::ReliableSet<Q>;
using DropsCrash = s::TypingContext<s::RoleState<P, s::OutQueue<>, s::Send<s::PeerMsg<Q, M, int>, s::End>>,
                                    s::RoleState<Q, s::OutQueue<>, s::Recv<s::PeerMsg<P, M, int>, s::End>>>;

constexpr int check_context() noexcept {
    c::ensure_crash_associated<DropsCrash, g::State<g::Roles<>, Guarded>, OnlyQ>();
    return 0;
}

}  // namespace neg_sess_crash_assoc_drops_crash_branch_types

using namespace neg_sess_crash_assoc_drops_crash_branch_types;

int main() { return check_context(); }
