// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-031 fixture: sanitize_path()'s admission domain is exactly
// Path<source::External> — the trust-boundary surface where operator
// bytes arrive.  Calling sanitize_path() with any *other* source tag
// (FromUser, FromConfig, FromDb, Internal, …) must red at the call
// site so the audit catalog stays grep-discoverable: every taint
// flavor crossing into Sanitized must be admitted by an explicit
// retag_policy<From, Sanitized> specialization.
//
// V-232 (planned) will introduce per-surface tags
// (FromUserPath / FromEnvPath / FromConfigPath) and bind each to a
// dedicated sanitizer.  Until then, sanitize_path() in safety/Path.h
// is the External-only entry point; this fixture pins that contract.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "invalid initialization".  sanitize_path takes its argument by
//   rvalue reference, so GCC words the refusal as an invalid
//   initialization of `Path<source::External>&&` from a
//   `Path<source::FromUser>` rather than as a conversion failure.  The
//   registration also admits the "could not convert" / "cannot convert"
//   / "no matching function" wordings, which is what the overload set
//   would emit if sanitize_path ever took its argument by value.

// Wrap.h re-exports the wrappers but not the tag namespaces, so the
// source tags need their own header.  Without it `fixy::tags` is
// undeclared and this fixture rejects on a name error instead of on
// sanitize_path's admission domain.
#include <crucible/fixy/Source.h>
#include <crucible/fixy/Wrap.h>

int main() {
    // Caller mints a FromUser-tagged path — a different taint flavor,
    // not in sanitize_path's domain.
    crucible::fixy::wrap::Path<crucible::fixy::tags::source::FromUser> from_user_path{
        "/tmp/crucible_neg_v031_wrong_source"};

    // Should FAIL: sanitize_path takes Path<External>&&, not
    // Path<FromUser>; the type system refuses the cross-tag call.
    [[maybe_unused]] auto sanitized_e = crucible::fixy::wrap::sanitize_path(std::move(from_user_path));
    return 0;
}
