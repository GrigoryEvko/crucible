// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// is_empty_choice is an alias template over the empty-choice algebra of
// the registry, so no user specialization admits a choice with no branch.
//
// Expected diagnostic: a specialization of an alias template.
#include <fixy/session/Protocol.h>

#include <type_traits>

namespace fixy::session {
template <>
struct is_empty_choice<Select<>> : std::false_type {};
}  // namespace fixy::session

int main() { return 0; }
