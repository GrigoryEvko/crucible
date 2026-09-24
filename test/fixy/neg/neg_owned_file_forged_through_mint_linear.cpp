// The second route, and the one that makes a passkey the wrong shape
// here.  A handle wrapped in Linear is built by fixy::mint_linear, a
// generic variadic forwarder that Qtt befriends for every T and every
// argument pack.  So the construction that matters does not happen in
// any mint at all — it happens inside Qtt, from arguments mint_linear
// forwarded.
//
// A private constructor plus a friend list naming a mint would have
// closed nothing, because the mint is not where the object is built.
// Making mint_linear a friend would have opened the door to every type
// in the tree.  What closes it is that there is no reachable constructor
// to forward TO: is_constructible_v<OwnedFile, FILE*> is false, so
// mint_linear's own requires-clause rejects the call.
//
// This fixture is the standing witness that it stays false.
//
// Verified before the fix: this TU compiled clean.

#include <fixy/OwnedFile.h>
#include <fixy/Qtt.h>

#include <cstdio>

int main() {
    auto forged = fixy::mint_linear<fixy::OwnedFile>(stdin);
    return forged.peek().is_open() ? 0 : 1;
}
