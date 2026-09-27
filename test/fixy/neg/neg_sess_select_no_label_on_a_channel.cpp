// select<I>(no_label) puts no label on the wire.  Over a channel the peer
// then reads the next payload as the label, and the two ends leave the
// protocol.  Only a carrier that states the Local network takes no_label,
// so the select over this end of a channel is refused.

#include <fixy/session/Handle.h>

#include <foundation/Pinned.h>
#include <foundation/effects/Ctx.h>

#include <utility>

namespace s = fixy::session;

namespace neg_sess_select_no_label_on_a_channel_types {
struct Pipe : ::foundation::Pinned<Pipe> {
    int slot = 0;
};
struct SelfEnd {
    Pipe* pipe = nullptr;
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};
struct PeerEnd {
    Pipe* pipe = nullptr;
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};
}  // namespace neg_sess_select_no_label_on_a_channel_types

using namespace neg_sess_select_no_label_on_a_channel_types;

using Choice = s::Select<s::Send<int, s::End>, s::End>;

int main() {
    Pipe pipe{};
    const ::foundation::effects::detail::ctx_witnesses::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto [self_head, peer_head] = s::mint_test_channel<Choice>(ctx, SelfEnd{&pipe}, PeerEnd{&pipe});
    std::move(self_head).select<1>(s::no_label);
    std::move(peer_head).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
