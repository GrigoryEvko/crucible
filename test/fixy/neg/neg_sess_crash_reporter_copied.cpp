// A cell has one reporter, so the report of a crash has one author.  A
// copy of the reporter would give the cell a second author.

#include <fixy/session/CrashTransport.h>

namespace s = ::fixy::session;

int main() {
    s::PeerCrashCell cell;
    auto reporter = s::mint_crash_reporter(cell);
    auto second = reporter;
    static_cast<void>(std::move(second).report(s::CrashCause::Abort));
    return 0;
}
