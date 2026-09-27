// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A translation unit specializes the checkpoint handle and builds, from
// the specialization, a checkpoint handle around a plain handle, with a
// checkpoint that no compliance check saw.  The checkpoint handle
// befriends only the checkpoint door, so no specialization of it reaches
// the constructor of another.
//
// Expected diagnostic: the constructor is private in this context.
#include <fixy/session/Checkpoint.h>

namespace neg_sess_checkpoint_handle_friend_specialized_types {
struct Tag {};
struct Wire {
    [[no_unique_address]] ::fixy::session::MoveOnlyResource one_holder{};
};
}  // namespace neg_sess_checkpoint_handle_friend_specialized_types

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

int main() {
    namespace s = ::fixy::session;
    using namespace neg_sess_checkpoint_handle_friend_specialized_types;
    auto plain = s::mint_session_handle<s::Send<int, s::End>>(Wire{});
    auto forged = s::CheckpointHandle<Tag, Tag, Tag, Tag>::forge(std::move(plain));
    std::move(forged).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
