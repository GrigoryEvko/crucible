// Negative compile test.  This file must fail to compile.
//
// A recorded handle names its protocol and its Resource, but it is not a
// plain, crash-watched or checkpoint handle.  A second recorder around it
// records each step of the session two times, and it has no protocol to
// read the label words from.  The mint rejects it.
//
// Expected diagnostic: the handle does not satisfy RecordableHandle.

#include <fixy/Ctx.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Recording.h>

namespace neg_sess_record_recorded_handle_types {
struct Wire {};
}  // namespace neg_sess_record_recorded_handle_types

int main() {
    namespace s = ::fixy::session;
    using namespace neg_sess_record_recorded_handle_types;
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    s::SessionEventLog first_log;
    s::SessionEventLog second_log;
    const ::foundation::effects::Alloc alloc = ::foundation::effects::testing::test().alloc;
    auto once =
        s::mint_recorded_session(s::mint_session<s::End>(ctx, Wire{}), s::mint_session_event_writer(alloc, first_log),
                                 s::RoleTagId{1}, s::RoleTagId{2});
    auto twice = s::mint_recorded_session(std::move(once), s::mint_session_event_writer(alloc, second_log),
                                          s::RoleTagId{1}, s::RoleTagId{2});
    static_cast<void>(std::move(twice).close());
    return 0;
}
