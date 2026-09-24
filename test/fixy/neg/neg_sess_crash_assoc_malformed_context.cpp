// The crash-stop association gate takes a configuration.  A Recv of a
// bare payload names no peer, so no rule of the configuration semantics
// reads it, and the gate refuses the arguments before it checks a clause.

#include <fixy/session/CrashAssociation.h>

// These types have external linkage.  The session folds their stable
// ids, and a stable id refuses a type with internal linkage.
namespace neg_sess_crash_assoc_malformed_context_types {

namespace g = ::fixy::session::global;
namespace s = ::fixy::session;
namespace c = ::fixy::session::config;

struct P {};

using Bare = s::TypingContext<s::RoleState<P, s::OutQueue<>, s::Recv<int, s::End>>>;

constexpr int check_context() noexcept {
    c::ensure_crash_associated<Bare, g::State<g::Roles<>, g::End>, s::NoReliableRoles>();
    return 0;
}

}  // namespace neg_sess_crash_assoc_malformed_context_types

using namespace neg_sess_crash_assoc_malformed_context_types;

int main() { return check_context(); }
