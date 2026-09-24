// A cell has one reporter, so the count it publishes has one source.  A
// copy of the reporter would give the cell a second author.

#include <fixy/session/CrashTransport.h>

namespace s = ::fixy::session;

int main() {
    s::PeerCrashCell cell;
    auto reporter = s::mint_crash_reporter(cell);
    auto second = reporter;
    static_cast<void>(std::move(second).report(s::CrashCause::Abort, s::MessageCount{0}));
    return 0;
}
