// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-031 fixture: Cipher::open() refuses a *Sanitized*-tagged
// Path at the trust boundary.  The constructor's job is to RUN the
// sanitizer; a caller that already-sanitized must NOT short-circuit
// past Cipher's own sanitize_path() invocation.
//
// `Tagged<T, Source>` is invariant in Source — Path<Sanitized> and
// Path<External> are DISTINCT C++ types.  Cipher::open's signature
// names Path<External> exactly; supplying Path<Sanitized> fails the
// type match at substitution time.
//
// This guards against a "clever caller" that thinks they can save
// Cipher the sanitize cost by pre-sanitizing — losing the invariant
// that Cipher's sanitized-path SOURCE is always Cipher's own dirfd
// root, never a caller-supplied alleged-Sanitized.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "no matching function" / "could not convert" / "cannot convert".

#include <crucible/Cipher.h>

int main() {
    // Caller mints a Sanitized-tagged path directly — pretending the
    // bytes were already validated.  Cipher::open MUST refuse this.
    crucible::fixy::wrap::Path<crucible::fixy::tags::source::Sanitized> already_sanitized{
        "/tmp/crucible_neg_v031_sanitized_input"};

    // Should FAIL: Path<Sanitized> ≠ Path<External>; the type system
    // refuses the cross-tag substitution.
    [[maybe_unused]] auto cipher =
        ::crucible::Cipher::open(std::move(already_sanitized));
    return 0;
}
