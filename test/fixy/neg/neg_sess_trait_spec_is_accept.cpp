// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// is_accept is an alias template that reads the head of the protocol
// from the registry, so no user specialization makes a Recv a take-over.
//
// Expected diagnostic: a specialization of an alias template.
#include <fixy/session/Delegate.h>

#include <type_traits>

namespace fixy::session {
template <>
struct is_accept<Recv<Send<int, End>, End>> : std::true_type {};
}  // namespace fixy::session

int main() { return 0; }
