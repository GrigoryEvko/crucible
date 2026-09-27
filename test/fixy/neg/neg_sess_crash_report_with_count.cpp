// A crash report takes no count.  The cell reads the count that the
// session of the watched endpoint wrote, so the failure detector cannot
// choose it.  A count of 0 would drop the messages still in the queue, and
// a count that is too high would make the survivor wait for a message that
// never comes.  A report that names a count does not compile.

#include <fixy/session/CrashTransport.h>

namespace s = fixy::session;

int main() {
    s::PeerCrashCell cell;
    static_cast<void>(s::mint_crash_reporter(cell).report(s::CrashCause::Abort, s::MessageCount{3}));
    return 0;
}
