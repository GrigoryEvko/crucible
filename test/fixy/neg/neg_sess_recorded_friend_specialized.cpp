// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A translation unit specializes the recorder and takes, from the
// specialization, the inner handle out of a live recorder.  The steps of
// that handle would then write nothing to the log.  The recorder
// befriends only the recording door, so no specialization of it reaches
// the inner handle of another.
//
// Expected diagnostic: the member is private in this context.
#include <fixy/session/Recording.h>

namespace neg_sess_recorded_friend_specialized_types {
struct Tag {};
struct Wire {
    [[no_unique_address]] ::fixy::session::MoveOnlyResource one_holder{};
};
}  // namespace neg_sess_recorded_friend_specialized_types

namespace fixy::session {
template <>
class Recorded<neg_sess_recorded_friend_specialized_types::Tag> {
public:
    template <typename H>
    static auto steal(Recorded<H>&& recorder) {
        return std::move(recorder.inner_);
    }
};
}  // namespace fixy::session

int main() {
    namespace s = ::fixy::session;
    using namespace neg_sess_recorded_friend_specialized_types;
    s::SessionEventLog log;
    auto recorded = s::mint_recorded_session(s::mint_session_handle<s::Send<int, s::End>>(Wire{}), log, s::RoleTagId{1},
                                             s::RoleTagId{2});
    auto stolen = s::Recorded<Tag>::steal(std::move(recorded));
    std::move(stolen).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
