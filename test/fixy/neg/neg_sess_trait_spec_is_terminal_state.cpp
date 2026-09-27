// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// is_terminal_state is an alias template over the terminal algebra of
// the registry, so no user specialization lets a handle close before the
// end of its protocol.
//
// Expected diagnostic: a specialization of an alias template.
#include <fixy/session/Protocol.h>

#include <type_traits>

namespace fixy::session {
template <>
struct is_terminal_state<Send<int, End>> : std::true_type {};
}  // namespace fixy::session

int main() { return 0; }
