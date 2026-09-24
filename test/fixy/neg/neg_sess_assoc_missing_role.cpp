// A typing context associates with a global type only when it holds an
// entry for each role of that type (Pischke, Masters, Yoshida,
// Definition 21).  Here Carol has no entry, so nobody runs her part and
// Bob waits for her for ever.
//
// Alice and Bob carry their projected types exactly, and the global type
// is balanced+, so the refusal comes from the domain clause.

#include <fixy/session/Projection.h>

namespace {

namespace g = ::fixy::session::global;
namespace s = ::fixy::session;

struct Alice {};
struct Bob {};
struct Carol {};
struct Job {};
struct Done {};

using Relay = g::Msg<Alice, Bob, Job, int, g::Msg<Carol, Bob, Done, int, g::End>>;

using WithoutCarol = s::TypingContext<s::RoleState<Alice, s::OutQueue<>, typename s::project_t<Relay, Alice>::local>,
                                      s::RoleState<Bob, s::OutQueue<>, typename s::project_t<Relay, Bob>::local>>;

constexpr int check_context() noexcept {
    s::ensure_associated<WithoutCarol, Relay>();
    return 0;
}

}  // namespace

int main() { return check_context(); }
