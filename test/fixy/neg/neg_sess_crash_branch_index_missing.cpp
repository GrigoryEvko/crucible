// The crash branch index exists only for an Offer that has a crash
// branch for the peer.

#include <fixy/session/CrashTransport.h>

namespace s = fixy::session;

namespace {
struct Alice {};
struct Bob {};
struct Wire {};
}  // namespace

using Unguarded = s::Offer<s::Recv<int, s::End>>;

int main() { return static_cast<int>(s::crash_branch_index_v<Unguarded, Bob>); }
