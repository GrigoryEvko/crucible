// A subtype handles no fewer branches than its supertype.  Carol's
// entry here drops Right, which Bob can select.  If Bob selects it,
// Carol reads a label she has no branch for.
//
// The domain and the queues are exact, so the refusal comes from the
// local-type clause.

#include <fixy/session/Projection.h>

namespace {

namespace g = ::fixy::session::global;
namespace s = ::fixy::session;

struct Alice {};
struct Bob {};
struct Carol {};
struct Go {};
struct Left {};
struct Right {};

using Relay = g::Msg<Alice, Bob, Go, int,
                     g::Comm<Bob, Carol, g::Branch<Left, int, g::End>, g::Branch<Right, int, g::End>>>;

using CarolNarrow = s::Offer<s::Sender<Bob>, s::Recv<s::PeerMsg<Bob, Left, int>, s::End>>;

using NarrowCarol = s::TypingContext<s::RoleState<Alice, s::OutQueue<>, typename s::project_t<Relay, Alice>::local>,
                                     s::RoleState<Bob, s::OutQueue<>, typename s::project_t<Relay, Bob>::local>,
                                     s::RoleState<Carol, s::OutQueue<>, CarolNarrow>>;

constexpr int check_context() noexcept {
    s::ensure_associated<NarrowCarol, Relay>();
    return 0;
}

}  // namespace

int main() { return check_context(); }
