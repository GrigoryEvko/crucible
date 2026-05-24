// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-FOUND-089 #2244 — HS14 mint-gate witness #1/2 for
// `fixy::substr::mpsc::mint_mpsc_consumer_endpoint<Channel>(
//      Channel&, Permission<consumer_tag>&&)`.
//
// Violation: pass a non-channel type (`int`).  Concept
// `MpscChannelSessionSurface Channel` rejects on missing nested
// type aliases; additionally, `Permission<typename Channel::
// consumer_tag>&&` cannot deduce `consumer_tag` from `int`.
//
// Pairs with neg_fixy_substr_mpsc_consumer_endpoint_wrong_perm_tag.cpp
// (channel-shape OK but wrong Permission tag) for the 2-fixture
// HS14 floor.  Distinct mismatch classes:
//   #1 (this) — concept fails on non-channel.
//   #2 (peer) — concept passes; function-parameter Permission tag
//               mismatch fails the call-site binding.
//
// Expected diagnostic: "constraints not satisfied" /
// "MpscChannelSessionSurface" / "no matching function" / "no type
// named" / "mint_mpsc_consumer_endpoint" / "consumer_tag".

#include <crucible/fixy/Substr.h>
#include <crucible/permissions/Permission.h>

#include <utility>

namespace fmpsc = ::crucible::fixy::substr::mpsc;

namespace neg_fixy_mpsc_consumer_non_channel {
struct StrayTag {};
}

int main() {
    int not_a_channel = 0;
    auto perm = ::crucible::safety::mint_permission_root<
        neg_fixy_mpsc_consumer_non_channel::StrayTag>();
    auto bad = fmpsc::mint_mpsc_consumer_endpoint(
        not_a_channel, std::move(perm));
    (void)bad;
    return 0;
}
