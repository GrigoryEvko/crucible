// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// is_offer is an alias template that reads the head of the protocol from
// the registry, so no user specialization lets a Select wait for a label.
//
// Expected diagnostic: a specialization of an alias template.
#include <fixy/session/Protocol.h>

#include <type_traits>

namespace fixy::session {
template <>
struct is_offer<Select<Send<int, End>>> : std::true_type {};
}  // namespace fixy::session

int main() { return 0; }
