// A send is covariant in its payload, and the axiom peer_message lifts
// the payload order to PeerMsg.  A value tagged FromUser has no edge to
// the bare type, because that would carry unchecked input into a
// position that assumes a check.  So Alice's entry, which sends a user
// value where the projection sends int, is refused.  A Sanitized value
// in the same place is associated.

#include <fixy/session/Projection.h>

namespace {

namespace g = ::fixy::session::global;
namespace s = ::fixy::session;

struct Alice {};
struct Bob {};
struct Ask {};

using Question = g::Msg<Alice, Bob, Ask, int, g::End>;

using Unchecked = ::fixy::Tagged<int, ::fixy::tags::source::FromUser>;
using Checked = ::fixy::Tagged<int, ::fixy::tags::source::Sanitized>;

template <typename Payload>
using AliceSends = s::TypingContext<s::RoleState<Alice, s::OutQueue<>, s::Send<s::PeerMsg<Bob, Ask, Payload>, s::End>>,
                                    s::RoleState<Bob, s::OutQueue<>, typename s::project_t<Question, Bob>::local>>;

static_assert(s::association_holds_v<AliceSends<Checked>, Question>);

constexpr int check_context() noexcept {
    s::ensure_associated<AliceSends<Unchecked>, Question>();
    return 0;
}

}  // namespace

int main() { return check_context(); }
