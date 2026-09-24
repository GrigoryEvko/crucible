// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-FOUND-089 #2244 — HS14 mint-gate witness #1/2 for
// `fixy::substr::mpsc::mint_mpsc_producer_endpoint<Channel>(Channel&)`.
//
// Violation: pass a non-channel type (`int`) — the concept
// `MpscChannelSessionSurface Channel` rejects on missing nested
// type aliases (`Channel::value_type`, `Channel::user_tag`, etc.).
//
// Pairs with neg_fixy_substr_mpsc_producer_endpoint_almost_channel.cpp
// (concept passes typedefs but fails on `ch.producer()` method
// shape) for the 2-fixture HS14 floor.  Distinct mismatch classes:
//   #1 (this) — concept fails at first typedef requirement.
//   #2 (peer) — concept fails at method-shape requirement.
//
// Expected diagnostic: "constraints not satisfied" /
// "MpscChannelSessionSurface" / "no matching function" / "no type
// named" / "value_type" / "mint_mpsc_producer_endpoint".

#include <crucible/fixy/Substr.h>

namespace fmpsc = ::crucible::fixy::substr::mpsc;

int main() {
    int not_a_channel = 0;
    auto bad = fmpsc::mint_mpsc_producer_endpoint(not_a_channel);
    (void)bad;
    return 0;
}
