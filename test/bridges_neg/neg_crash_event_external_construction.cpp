// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-021: companion to neg_wrap_crash_return_key_external_construction.cpp.
// Witnesses the pass-key idiom end-to-end: external code attempting to
// bypass `wrap_crash_return` by spelling `CrashEvent<P, R>{key, ...}`
// directly cannot synthesize the required `WrapCrashReturnKey` first
// argument.  The private default ctor of `WrapCrashReturnKey` is only
// friended on `detail::WrapCrashReturnAuthorizer`, so the value-init
// `{}` for the first ctor argument fails at template-argument
// deduction / overload resolution time.
//
// The brittleness target HS14 pins: a future edit that adds
// `std::source_location loc = std::source_location::current()` to
// `wrap_crash_return` (the standard idiom elsewhere in this codebase)
// would have silently de-friended the variadic-function-template form;
// the post-A2-021 class-identity friend on the non-template authorizer
// is immune to such drift.  This fixture witnesses that the protection
// holds.
//
// FIXY-V-013 evolution: this fixture exercises the A2-021 barrier
// (the passkey's private default ctor).  V-013 added an INDEPENDENT
// second barrier — CrashEvent's ctor itself moved to `private:` with
// friendship on `detail::WrapCrashReturnAuthorizer` (the `mint_event_`
// templated member function is the sole grep-discoverable
// construction site for CrashEvent in the entire tree).  The
// `neg_crash_event_ctor_public_bypass.cpp` companion exercises that
// second barrier directly: it mints a well-formed key via the public
// `mint()` surface and witnesses that direct ctor invocation still
// fails because the ctor is now private.  Defense-in-depth.
//
// Expected diagnostic family (one or more should match):
//   "is private within this context"  |  "private"  |
//   "WrapCrashReturnKey"

#include <crucible/bridges/CrashTransport.h>

#include <tuple>

struct ExternalPeerTag {};
struct ExternalResource {};

[[maybe_unused]] void probe() {
    using Event = ::crucible::safety::proto::CrashEvent<
        ExternalPeerTag, ExternalResource>;
    auto bad = Event{{}, ExternalResource{}, std::tuple<>{}};
    (void)bad;
}
