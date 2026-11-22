// Negative compile test.  This file must fail to compile.
//
// A log has one writer.  Two recorders that take one log by reference
// append to it from two threads with no order.  The recorder mint takes
// the one writer of a log, and a log alone gives no right to append.
//
// Expected diagnostic: the overload that takes a log is deleted, with the
// tag Event_Log_Needs_Its_Writer.

#include <fixy/Ctx.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Recording.h>

namespace neg_sess_record_log_without_writer_types {
struct Wire {};
}  // namespace neg_sess_record_log_without_writer_types

int main() {
    namespace s = ::fixy::session;
    using namespace neg_sess_record_log_without_writer_types;
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    s::SessionEventLog shared_log;
    auto recorded =
        s::mint_recorded_session(s::mint_session<s::End>(ctx, Wire{}), shared_log, s::RoleTagId{1}, s::RoleTagId{2});
    static_cast<void>(std::move(recorded).close());
    return 0;
}
