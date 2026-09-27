// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// is_select is an alias template that reads the head of the protocol
// from the registry, so no user specialization lets an Offer pick.
//
// Expected diagnostic: a specialization of an alias template.
#include <fixy/session/Protocol.h>

#include <type_traits>

namespace fixy::session {
template <>
struct is_select<Offer<Recv<int, End>>> : std::true_type {};
}  // namespace fixy::session

int main() { return 0; }
