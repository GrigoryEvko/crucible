// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture for FIXY-V-013 (§XXI Universal Mint Pattern compliance).
//
// Premise: pre-V-013, `CrashEvent<P, R>`'s ctor was public.  The only
// barrier to direct construction was that the first parameter
// `WrapCrashReturnKey` has a private default ctor friended only on
// `detail::WrapCrashReturnAuthorizer`.  The existing fixture
// `neg_crash_event_external_construction.cpp` witnesses that an
// attacker who tries `Event{{}, r, perms}` fails at the value-init
// of the key.
//
// V-013 closes a *different* bypass: a hostile-but-well-intentioned
// caller (or an honest production-side refactor) could mint a key
// through the public authorizer surface and feed it to CrashEvent's
// public ctor — bypassing `wrap_crash_return`, the only legitimate
// authorization point:
//
//   auto key = detail::WrapCrashReturnAuthorizer::mint();   // public
//   CrashEvent<P, R>{std::move(key), r, perms};             // PRE-V-013: legal
//
// This bypass smuggles a CrashEvent into existence WITHOUT executing
// `inner.detach(reason_tag)` (the dynamic death witness that
// `wrap_crash_return` performs), which fixy-A2-021 and #430 made
// load-bearing.  The compile-only fixture
// `neg_crash_event_direct_construction.cpp` does not catch this path
// because it spells `CrashEvent<P, R>{recovered}` (one arg, key absent)
// not `CrashEvent<P, R>{key, recovered, perms}` (key obtained from
// public authorizer mint).
//
// V-013 fix: CrashEvent's ctor moved to `private:` with
// `friend struct detail::WrapCrashReturnAuthorizer;`.  The sole
// grep-discoverable construction site is
// `detail::WrapCrashReturnAuthorizer::mint_event<Event>(r, perms)`,
// invoked by `wrap_crash_return`.  Direct construction with a hand-
// minted key is now ill-formed at the access-control check, with
// diagnostic mentioning "is private within this context".
//
// Defense-in-depth rationale: pre-V-013 the WrapCrashReturnKey's
// unforgeability was the ONLY barrier.  Post-V-013 the ctor access
// check is an INDEPENDENT barrier — even a forged key, an unsafe
// `bit_cast`, or an accidentally-public authorizer evolution cannot
// produce a CrashEvent outside the authorized mint path.
//
// Expected diagnostic family (one or more should match):
//   "is private within this context"  |  "private"

#include <crucible/bridges/CrashTransport.h>

#include <tuple>
#include <utility>

struct ExternalPeerTag {};
struct ExternalResource {};

[[maybe_unused]] void probe() {
    using Event = ::crucible::safety::proto::CrashEvent<
        ExternalPeerTag, ExternalResource>;

    // The §XXI bypass attempt the V-013 fix closes:
    //
    //   Step 1: obtain a WrapCrashReturnKey via the public authorizer
    //   surface.  This call is legitimate — `WrapCrashReturnAuthorizer`
    //   is the documented mint point for the passkey.  Pre-V-013 a
    //   hostile caller (or an honest refactor that drifted away from
    //   `wrap_crash_return`) could obtain a key this way.
    auto key = ::crucible::safety::proto::detail::
        WrapCrashReturnAuthorizer::mint();

    //   Step 2: feed the well-formed key to CrashEvent's ctor directly,
    //   bypassing `wrap_crash_return`'s `inner.detach(reason_tag)`
    //   side effect.  Pre-V-013 this compiled silently.  With V-013
    //   the ctor is private and the access check fires here.
    auto bad = Event{std::move(key), ExternalResource{}, std::tuple<>{}};
    (void)bad;
}
