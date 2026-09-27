// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// is_reliable_set is an alias of a reflection query, so no user
// specialization makes a type that is no ReliableSet pass the first
// clause of the crash mint.
//
// Expected diagnostic: a specialization of an alias template.
#include <fixy/session/Crash.h>

#include <type_traits>

namespace neg_sess_trait_spec_is_reliable_set_types {
struct EveryRole {};
}  // namespace neg_sess_trait_spec_is_reliable_set_types

namespace fixy::session {
template <>
struct is_reliable_set<neg_sess_trait_spec_is_reliable_set_types::EveryRole> : std::true_type {};
}  // namespace fixy::session

int main() { return 0; }
