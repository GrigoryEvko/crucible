// The global type says that Alice already sent Ask, so her queue to Bob
// holds it.  The context below has an empty queue for Alice, and Bob
// then waits for a message that nobody will send.
//
// Each role carries its projected local type, so the refusal comes from
// the queue clause.

#include <fixy/session/Liveness.h>

namespace {

namespace g = ::fixy::session::global;
namespace s = ::fixy::session;

struct Alice {};
struct Bob {};
struct Ask {};

using Sent = g::EnRoute<Alice, Bob, Ask, int, g::End>;

using EmptyQueue = s::TypingContext<s::RoleState<Alice, s::OutQueue<>, typename s::project_t<Sent, Alice>::local>,
                                    s::RoleState<Bob, s::OutQueue<>, typename s::project_t<Sent, Bob>::local>>;

constexpr int require_live() noexcept {
    s::ensure_context_live<EmptyQueue, Sent>();
    return 0;
}

}  // namespace

int main() { return require_live(); }
