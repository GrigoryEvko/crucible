// The global type must be balanced+.  Equation (49) of Pischke, Masters
// and Yoshida puts a second message from P to Q en route below a
// transmission from P to Q, so the en-route count is undefined.  Its
// projected context matches each projection and is still unsafe.

#include <fixy/session/CrashAssociation.h>

// These types have external linkage.  The session folds their stable
// ids, and a stable id refuses a type with internal linkage.
namespace neg_sess_crash_assoc_not_balanced_plus_types {

namespace g = ::fixy::session::global;
namespace s = ::fixy::session;
namespace c = ::fixy::session::config;

struct P {};
struct Q {};
struct M {};
struct M1 {};

using Ex49 = g::Msg<P, Q, M, int, g::EnRoute<P, Q, M1, int, g::End>>;

constexpr int check_context() noexcept {
    c::ensure_crash_associated<s::projected_context_t<Ex49>, g::State<g::Roles<>, Ex49>, s::EveryRoleReliable>();
    return 0;
}

}  // namespace neg_sess_crash_assoc_not_balanced_plus_types

using namespace neg_sess_crash_assoc_not_balanced_plus_types;

int main() { return check_context(); }
