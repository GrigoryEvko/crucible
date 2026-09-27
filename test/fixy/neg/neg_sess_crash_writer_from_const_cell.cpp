// The writer of a cell counts the messages of the endpoint that the cell
// watches.  A const cell is the view that the peer reads, and a view
// gives no right to count, so a writer minted from it does not compile.

#include <fixy/session/CrashTransport.h>

namespace s = fixy::session;

int main() {
    s::PeerCrashCell cell;
    const s::PeerCrashCell& view = cell;
    s::mint_crash_writer(view);
    return 0;
}
