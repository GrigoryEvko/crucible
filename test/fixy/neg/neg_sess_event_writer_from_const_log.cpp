// Negative compile test.  This file must fail to compile.
//
// A const log is a view of the record.  A view gives no right to append,
// so the mint of the writer refuses it.
//
// Expected diagnostic: the overload that takes a const log is deleted,
// with the tag Event_Log_Writer_From_Const_Log.

#include <fixy/session/EventLog.h>
#include <foundation/effects/Effect.h>

int main() {
    namespace s = ::fixy::session;
    const s::SessionEventLog log;
    auto writer = s::mint_session_event_writer(::foundation::effects::testing::test().alloc, log);
    writer.record_now(s::SessionEvent::close(s::RoleTagId{1}, s::RoleTagId{2}));
    return 0;
}
