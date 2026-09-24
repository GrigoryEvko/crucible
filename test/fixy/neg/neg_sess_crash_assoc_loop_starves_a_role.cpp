// The global type must be balanced+, and so balanced.  R acts only after
// the loop ends, so a path that stays in the loop never meets R.  The
// gate refuses the type before it reads the context.

#include <fixy/session/CrashAssociation.h>

// These types have external linkage.  The session folds their stable
// ids, and a stable id refuses a type with internal linkage.
namespace neg_sess_crash_assoc_loop_starves_a_role_types {

namespace g = ::fixy::session::global;
namespace s = ::fixy::session;
namespace c = ::fixy::session::config;

struct P {};
struct Q {};
struct R {};
struct M {};
struct M1 {};
struct M2 {};

using Starves = g::Rec<g::Comm<P, Q, g::Branch<M1, int, g::Var>, g::Branch<M2, int, g::Msg<P, R, M, int, g::End>>>>;

constexpr int check_context() noexcept {
    c::ensure_crash_associated<s::TypingContext<>, g::State<g::Roles<>, Starves>, s::EveryRoleReliable>();
    return 0;
}

}  // namespace neg_sess_crash_assoc_loop_starves_a_role_types

using namespace neg_sess_crash_assoc_loop_starves_a_role_types;

int main() { return check_context(); }
