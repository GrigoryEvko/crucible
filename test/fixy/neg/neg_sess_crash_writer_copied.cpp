// One session counts into a cell.  The writer of the cell is move-only,
// so two sessions cannot hold it: a copy of it does not compile.

#include <fixy/session/CrashTransport.h>

namespace s = fixy::session;

int main() {
    s::PeerCrashCell cell;
    auto writer = s::mint_crash_writer(cell);
    auto second = writer;
    static_cast<void>(second);
    return 0;
}
