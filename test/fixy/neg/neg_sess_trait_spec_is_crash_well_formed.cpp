// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// is_crash_well_formed is an alias of the answer of the crash walk, so no
// user specialization admits a protocol that sends the crash label.
//
// Expected diagnostic: a specialization of an alias template.
#include <fixy/session/Crash.h>

#include <type_traits>

namespace neg_sess_trait_spec_is_crash_well_formed_types {
struct Peer {};
}  // namespace neg_sess_trait_spec_is_crash_well_formed_types

namespace fixy::session {
template <>
struct is_crash_well_formed<Send<Crash<neg_sess_trait_spec_is_crash_well_formed_types::Peer>, End>> : std::true_type {};
}  // namespace fixy::session

int main() { return 0; }
