// The entries at Stop are exactly the crashed roles (clause A2 of
// Definition 4.19).  P is at Stop, and the state says that no role
// crashed.

#include <fixy/session/CrashAssociation.h>

// These types have external linkage.  The session folds their stable
// ids, and a stable id refuses a type with internal linkage.
namespace neg_sess_crash_assoc_stopped_but_live_types {

namespace g = ::fixy::session::global;
namespace s = ::fixy::session;
namespace c = ::fixy::session::config;

struct P {};
struct Q {};
struct M {};

using Guarded = g::Comm<P, Q, g::Branch<M, int, g::End>, g::Branch<g::CrashLabel, void, g::End>>;
using OnlyQ = s::ReliableSet<Q>;
using CrashedCtx = c::step_t<c::crash_projected_context_t<Guarded, OnlyQ>, g::CrashAction<P>, OnlyQ>;

constexpr int check_context() noexcept {
    c::ensure_crash_associated<CrashedCtx, g::State<g::Roles<>, Guarded>, OnlyQ>();
    return 0;
}

}  // namespace neg_sess_crash_assoc_stopped_but_live_types

using namespace neg_sess_crash_assoc_stopped_but_live_types;

int main() { return check_context(); }
