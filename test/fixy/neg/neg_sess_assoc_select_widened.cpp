// Association refines each entry with synchronous subtyping, and a
// subtype selects from no more branches than its supertype.  Bob's entry
// here can also select Stop, which Carol does not offer.  If Bob sends
// it, Carol reads a label she has no branch for.
//
// The same context without the Stop branch, or with Carol also offering
// Stop at the end, is associated.  The domain and the queues are exact,
// so the refusal comes from the local-type clause.

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
struct Stop {};

using Relay = g::Msg<Alice, Bob, Go, int,
                     g::Comm<Bob, Carol, g::Branch<Left, int, g::End>, g::Branch<Right, int, g::End>>>;

using BobWide = s::Recv<s::PeerMsg<Alice, Go, int>,
                        s::Select<s::Send<s::PeerMsg<Carol, Left, int>, s::End>, s::Send<s::PeerMsg<Carol, Right, int>, s::End>,
                                  s::Send<s::PeerMsg<Carol, Stop, int>, s::End>>>;

using WideBob = s::TypingContext<s::RoleState<Alice, s::OutQueue<>, typename s::project_t<Relay, Alice>::local>,
                                 s::RoleState<Bob, s::OutQueue<>, BobWide>,
                                 s::RoleState<Carol, s::OutQueue<>, typename s::project_t<Relay, Carol>::local>>;

constexpr int check_context() noexcept {
    s::ensure_associated<WideBob, Relay>();
    return 0;
}

}  // namespace

int main() { return check_context(); }
