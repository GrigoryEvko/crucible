// The entries at Stop are exactly the crashed roles (clause A2 of
// Definition 4.19).  The state says that P crashed, and the entry of P
// still sends.

#include <fixy/session/CrashAssociation.h>

// These types have external linkage.  The session folds their stable
// ids, and a stable id refuses a type with internal linkage.
namespace neg_sess_crash_assoc_crashed_but_running_types {

namespace g = ::fixy::session::global;
namespace s = ::fixy::session;
namespace c = ::fixy::session::config;

struct P {};
struct Q {};
struct M {};

using Guarded = g::Comm<P, Q, g::Branch<M, int, g::End>, g::Branch<g::CrashLabel, void, g::End>>;
using OnlyQ = s::ReliableSet<Q>;
using Crashed = g::state_step_t<g::State<g::Roles<>, Guarded>, g::CrashAction<P>, OnlyQ>;

constexpr int check_context() noexcept {
    c::ensure_crash_associated<c::crash_projected_context_t<Guarded, OnlyQ>, Crashed, OnlyQ>();
    return 0;
}

}  // namespace neg_sess_crash_assoc_crashed_but_running_types

using namespace neg_sess_crash_assoc_crashed_but_running_types;

int main() { return check_context(); }
