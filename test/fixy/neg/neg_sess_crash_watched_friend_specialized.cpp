// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A translation unit specializes the crash decorator and builds, from the
// specialization, a decorator around a plain handle.  The protocol of the
// handle has a bare reception from an unreliable peer, which the crash
// mint refuses.  The decorator befriends only the crash door, so no
// specialization of it reaches the constructor of another.
//
// Expected diagnostic: the constructor is private in this context.
#include <fixy/session/CrashTransport.h>

namespace neg_sess_crash_watched_friend_specialized_types {
struct Tag {};
struct Alice {};
struct Bob {};
struct Wire {
    [[no_unique_address]] ::fixy::session::MoveOnlyResource one_holder{};
};
}  // namespace neg_sess_crash_watched_friend_specialized_types

namespace fixy::session {
template <>
class CrashWatched<neg_sess_crash_watched_friend_specialized_types::Tag,
                   neg_sess_crash_watched_friend_specialized_types::Tag,
                   neg_sess_crash_watched_friend_specialized_types::Tag, ReliableSet<>,
                   neg_sess_crash_watched_friend_specialized_types::Tag, detail::crash_transport::between_messages> {
public:
    template <typename H>
    static auto forge(H inner, const PeerCrashCell& cell, PeerCrashCell& own) {
        return CrashWatched<H, neg_sess_crash_watched_friend_specialized_types::Alice,
                            neg_sess_crash_watched_friend_specialized_types::Bob, ReliableSet<>,
                            neg_sess_crash_watched_friend_specialized_types::Tag>{
            std::move(inner), cell, own, neg_sess_crash_watched_friend_specialized_types::Tag{}};
    }
};
}  // namespace fixy::session

int main() {
    namespace s = ::fixy::session;
    using namespace neg_sess_crash_watched_friend_specialized_types;
    s::PeerCrashCell cell;
    s::PeerCrashCell own;
    auto plain = s::mint_session_handle<s::Recv<int, s::End>>(Wire{});
    auto forged = s::CrashWatched<Tag, Tag, Tag, s::ReliableSet<>, Tag>::forge(std::move(plain), cell, own);
    std::move(forged).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
