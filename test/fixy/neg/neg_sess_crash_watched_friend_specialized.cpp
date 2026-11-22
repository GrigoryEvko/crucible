// Negative compile test.  This file must fail to compile.
//
// A translation unit specializes the crash decorator and builds, from the
// specialization, a decorator around a plain handle.  The protocol of the
// handle has a bare reception from an unreliable peer, which the crash
// mint refuses.  The decorator befriends only the crash door, so no
// specialization of it reaches the constructor of another.
//
// Expected diagnostic: the constructor is private in this context.
#include <fixy/Ctx.h>
#include <fixy/session/CrashTransport.h>
#include <fixy/session/Entry.h>
#include <foundation/Quarantine.h>

namespace neg_sess_crash_watched_friend_specialized_types {
struct Tag {};
struct Alice {};
struct Bob {};
struct Wire {
    [[no_unique_address]] ::fixy::session::MoveOnlyResource one_holder{};
};
}  // namespace neg_sess_crash_watched_friend_specialized_types

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture forges a CrashWatched that builds a decorator around a plain handle")
namespace fixy::session {
template <>
class CrashWatched<neg_sess_crash_watched_friend_specialized_types::Tag,
                   neg_sess_crash_watched_friend_specialized_types::Tag,
                   neg_sess_crash_watched_friend_specialized_types::Tag, ReliableSet<>,
                   neg_sess_crash_watched_friend_specialized_types::Tag, detail::crash_transport::between_messages> {
public:
    template <typename H>
    static auto forge(H inner, const CrashCellCore& cell, CrashCellCore& own) {
        return CrashWatched<H, neg_sess_crash_watched_friend_specialized_types::Alice,
                            neg_sess_crash_watched_friend_specialized_types::Bob, ReliableSet<>,
                            neg_sess_crash_watched_friend_specialized_types::Tag>{
            std::move(inner), cell, own, nullptr, neg_sess_crash_watched_friend_specialized_types::Tag{}};
    }
};
}  // namespace fixy::session
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() {
    namespace s = ::fixy::session;
    using namespace neg_sess_crash_watched_friend_specialized_types;
    s::CrashCell<Bob, Alice> cell;
    s::CrashCell<Alice, Bob> own;
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto plain = s::mint_session<s::Recv<int, s::End>>(ctx, Wire{});
    auto forged = s::CrashWatched<Tag, Tag, Tag, s::ReliableSet<>, Tag>::forge(std::move(plain), cell, own);
    std::move(forged).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
