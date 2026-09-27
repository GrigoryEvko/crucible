// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// is_delegate is an alias template that reads the head of the protocol
// from the registry, so no user specialization makes a Send a hand-off.
//
// Expected diagnostic: a specialization of an alias template.
#include <fixy/session/Delegate.h>

#include <type_traits>

namespace fixy::session {
template <>
struct is_delegate<Send<Send<int, End>, End>> : std::true_type {};
}  // namespace fixy::session

int main() { return 0; }
