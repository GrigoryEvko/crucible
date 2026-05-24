// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-FOUND-089 #2244 — HS14 mint-gate witness #2/2 for
// `fixy::substr::mpsc::mint_mpsc_consumer_endpoint<Channel>(
//      Channel&, Permission<consumer_tag>&&)`.
//
// Violation: channel is a real `PermissionedMpscChannel<int, 64,
// UserTag>` whose `consumer_tag` is the channel-internal sentinel
// type; but the caller passes `Permission<StrangerTag>` instead.
// Concept `MpscChannelSessionSurface` passes; function-parameter
// type for slot #2 is `Permission<typename Channel::consumer_tag>&&`
// which cannot bind a `Permission<StrangerTag>` rvalue (the typed
// permission family is provenance-strict — distinct tags are
// distinct types).
//
// Distinct mismatch class from the non-channel fixture: there the
// concept gate fires; here the concept passes and the binding to
// the templated function-parameter type fails.
//
// Expected diagnostic: "could not convert" / "cannot bind" / "no
// matching function" / "Permission" / "consumer_tag" /
// "mint_mpsc_consumer_endpoint".

#include <crucible/fixy/Substr.h>
#include <crucible/permissions/Permission.h>

#include <utility>

namespace fmpsc = ::crucible::fixy::substr::mpsc;

namespace neg_fixy_mpsc_consumer_wrong_perm {
struct UserTag {};
struct StrangerTag {};
using Channel = fmpsc::PermissionedMpscChannel<int, 64, UserTag>;
}  // namespace neg_fixy_mpsc_consumer_wrong_perm

int main() {
    using namespace neg_fixy_mpsc_consumer_wrong_perm;
    Channel* ch_ptr = nullptr;
    auto wrong_perm =
        ::crucible::safety::mint_permission_root<StrangerTag>();
    auto bad = fmpsc::mint_mpsc_consumer_endpoint(
        *ch_ptr, std::move(wrong_perm));
    (void)bad;
    return 0;
}
