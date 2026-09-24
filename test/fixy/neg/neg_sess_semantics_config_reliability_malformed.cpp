// The reliability assumption of a configuration is a ReliableSet or
// EveryRoleReliable.  A bare role is neither, so the alias refuses it.

#include <fixy/session/Semantics.h>

namespace {

struct P {};

namespace s = ::fixy::session;

using Labels = s::config::enabled_t<s::TypingContext<>, P>;

}  // namespace

int main() { return 0; }
