// mint_session asks for an execution context as its first argument.  An
// int is not one, so the gate refuses the call before any handle exists.

#include <fixy/session/Entry.h>

#include <utility>

namespace not_a_context_fixture {
namespace s = ::fixy::session;
struct Wire {};
using Proto = s::Send<int, s::End>;
}  // namespace not_a_context_fixture

int main() {
    auto head = ::fixy::session::mint_session<not_a_context_fixture::Proto>(0, not_a_context_fixture::Wire{});
    std::move(head).detach(::fixy::session::detach_reason::TestInstrumentation{});
    return 0;
}
