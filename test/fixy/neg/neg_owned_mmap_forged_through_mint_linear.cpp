// The second route, and the one that makes a passkey the wrong shape
// here.  Every mapping the surface hands out is wrapped in Linear, and
// Linear is built by fixy::mint_linear, a generic variadic forwarder
// that Qtt befriends for every T and every argument pack.  So the
// construction that matters does not happen in the mint at all — it
// happens inside Qtt, from arguments mint_linear forwarded.
//
// A private constructor plus a friend list naming the two mints would
// have closed nothing, because neither mint is where the object is
// built.  Making mint_linear a friend would have opened the door to
// every type in the tree.  What closes it is that there is no reachable
// constructor to forward TO: is_constructible_v<Region, void*, size_t>
// is false, so mint_linear's own requires-clause rejects the call.
//
// This fixture is the standing witness that it stays false.
//
// Verified before the fix: this TU compiled.  It was never run.

#include <fixy/OwnedMmap.h>
#include <fixy/Qtt.h>

#include <cstddef>

namespace {
struct ForwardedRegionTag final {};
struct AnyProt final {};
struct AnyShare final {};
using Region = fixy::OwnedMmap<ForwardedRegionTag, AnyProt, AnyShare>;
alignas(4096) char not_a_mapping[8192];
}  // namespace

int main() {
    auto forged = fixy::mint_linear<Region>(static_cast<void*>(not_a_mapping), sizeof(not_a_mapping));
    return forged.peek().is_mapped() ? 0 : 1;
}
