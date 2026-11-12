// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// is_crash_session_admissible is an alias template over a function that
// is not a template, so no user specialization admits a reception with no
// crash branch from an unreliable peer.
//
// Expected diagnostic: a specialization of an alias template.
#include <fixy/session/CrashTransport.h>

#include <type_traits>

namespace neg_sess_trait_spec_is_crash_session_admissible_types {
struct Alice {};
struct Bob {};
using Bare = ::fixy::session::Recv<int, ::fixy::session::End>;
}  // namespace neg_sess_trait_spec_is_crash_session_admissible_types

namespace fixy::session {
template <>
struct is_crash_session_admissible<
    CrashSession<neg_sess_trait_spec_is_crash_session_admissible_types::Bare,
                 neg_sess_trait_spec_is_crash_session_admissible_types::Alice,
                 neg_sess_trait_spec_is_crash_session_admissible_types::Bob, ReliableSet<>>> : std::true_type {};
}  // namespace fixy::session

int main() { return 0; }
