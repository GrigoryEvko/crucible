// The crash-stop association gate takes a ReliableSet or
// EveryRoleReliable.  A bare role list is neither, so the gate refuses
// the arguments before it checks a clause.

#include <fixy/session/CrashAssociation.h>

// These types have external linkage.  The session folds their stable
// ids, and a stable id refuses a type with internal linkage.
namespace neg_sess_crash_assoc_malformed_reliability_types {

namespace g = ::fixy::session::global;
namespace s = ::fixy::session;
namespace c = ::fixy::session::config;

struct P {};

constexpr int check_context() noexcept {
    c::ensure_crash_associated<s::TypingContext<>, g::State<g::Roles<>, g::End>, g::Roles<P>>();
    return 0;
}

}  // namespace neg_sess_crash_assoc_malformed_reliability_types

using namespace neg_sess_crash_assoc_malformed_reliability_types;

int main() { return check_context(); }
