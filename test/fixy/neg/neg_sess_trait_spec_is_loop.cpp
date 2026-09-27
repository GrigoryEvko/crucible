// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// is_loop is an alias template that reads the head of the protocol from
// the registry, so no user specialization hides a Loop from the factory.
//
// Expected diagnostic: a specialization of an alias template.
#include <fixy/session/Protocol.h>

#include <type_traits>

namespace fixy::session {
template <>
struct is_loop<Loop<Send<int, Continue>>> : std::false_type {};
}  // namespace fixy::session

int main() { return 0; }
