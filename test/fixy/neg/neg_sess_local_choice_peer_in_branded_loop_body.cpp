// A local choice puts no label on a wire, so the protocol around it must
// name no peer.  Here the body of the loop sends to a peer before the
// choice.  The handle at the choice holds the protocol from the choice on,
// and that part names no peer.  The callback entry lends the handle with a
// session brand around the loop, so the admission must read the body of the
// loop through the brand.  It sees the peer, and it refuses the choice.

#include <fixy/Ctx.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Handle.h>
#include <fixy/session/Projection.h>

#include <cstddef>
#include <utility>

namespace s = fixy::session;

// The role and the label key feed a stable id, so each has external linkage.
namespace neg_sess_local_choice_peer_in_branded_loop_body_types {
struct Peer {};
struct Key {};
struct Counter {
    static constexpr s::Network session_network = s::Network::Local;
    [[no_unique_address]] s::MoveOnlyResource move_only{};
    int sent = 0;
};
}  // namespace neg_sess_local_choice_peer_in_branded_loop_body_types

using namespace neg_sess_local_choice_peer_in_branded_loop_body_types;

using Body = s::Send<s::PeerMsg<Peer, Key, int>, s::Select<s::Send<int, s::Continue>, s::End>>;
using Stream = s::Loop<Body>;

constexpr auto count_word = [](Counter& counter, std::size_t) noexcept {
    ++counter.sent;
    return true;
};
constexpr auto count_value = [](Counter& counter, int&) noexcept {
    ++counter.sent;
    return true;
};

// The body names the type of the handle at End, so the entry point reads no
// statement of the body to find it, and the refusal of the choice is the
// one error.
template <class Head>
using EndOf = s::SessionHandle<s::End, Counter, typename Head::loop_ctx, typename Head::abandonment_policy,
                               typename Head::perm_set>;

int main() {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    Counter back = s::with_session<Stream>(ctx, Counter{}, [](auto head) noexcept -> EndOf<decltype(head)> {
        auto at_value = std::move(head).send(count_word);
        auto at_choice = std::move(at_value).send(7, count_value);
        return std::move(at_choice).template select<1>(s::no_label);
    });
    static_cast<void>(back);
    return 0;
}
