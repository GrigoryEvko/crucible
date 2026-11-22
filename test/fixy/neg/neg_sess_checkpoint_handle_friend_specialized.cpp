// Negative compile test.  This file must fail to compile.
//
// A translation unit specializes the checkpoint handle and builds, from
// the specialization, a checkpoint handle around a plain handle, with a
// checkpoint that no compliance check saw.  The checkpoint handle
// befriends only the checkpoint door, so no specialization of it reaches
// the constructor of another.
//
// Expected diagnostic: the constructor is private in this context.
#include <fixy/Ctx.h>
#include <fixy/session/Checkpoint.h>
#include <fixy/session/Entry.h>
#include <foundation/Quarantine.h>

namespace neg_sess_checkpoint_handle_friend_specialized_types {
struct Tag {};
struct Wire {
    [[no_unique_address]] ::fixy::session::MoveOnlyResource one_holder{};
};
}  // namespace neg_sess_checkpoint_handle_friend_specialized_types

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture forges a CheckpointHandle that builds a handle around a plain one")
namespace fixy::session {
template <>
class CheckpointHandle<neg_sess_checkpoint_handle_friend_specialized_types::Tag,
                       neg_sess_checkpoint_handle_friend_specialized_types::Tag,
                       neg_sess_checkpoint_handle_friend_specialized_types::Tag,
                       neg_sess_checkpoint_handle_friend_specialized_types::Tag> {
public:
    template <typename H>
    static auto forge(H inner) {
        return CheckpointHandle<H, typename H::protocol, void, CheckpointFrame<Roll, Roll, void>>{std::move(inner)};
    }
};
}  // namespace fixy::session
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() {
    namespace s = ::fixy::session;
    using namespace neg_sess_checkpoint_handle_friend_specialized_types;
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto plain = s::mint_session<s::Send<int, s::End>>(ctx, Wire{});
    auto forged = s::CheckpointHandle<Tag, Tag, Tag, Tag>::forge(std::move(plain));
    std::move(forged).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
