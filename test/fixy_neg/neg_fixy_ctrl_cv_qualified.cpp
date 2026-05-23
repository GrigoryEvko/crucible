// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-244 HS14 fixture 3/3 — ctrl grant tags obey grant cv-purity.
//
// `IsGrantTag_v<G>` requires `is_same_v<G, remove_cvref_t<G>>` (closed by
// fixy-A4-033): a cv-qualified grant can only be PRODUCED by copy-pasting a
// runtime variable's `decltype` (`const auto g = builtin_trap_ok{};`), a
// code smell the type system flags rather than silently coerces.  The
// V-244 ctrl grant tags inherit that discipline through `grant_base`; a
// `const grant::ctrl::builtin_trap_ok` is NOT a valid grant tag.
//
// Mismatch class for HS14 audit: IsGrantTag CV-PURITY rejection — distinct
// from the abort missing-rationale arity path (fixture 1) and the abort
// non-string conversion path (fixture 2).  This fixture exercises the
// shared grant-tag purity gate from the ctrl-family side.
//
// Expected diagnostic: the static_assert message below (IsGrantTag).

#include <crucible/fixy/grant/Ctrl.h>

namespace gr   = crucible::fixy::grant;
namespace ctrl = crucible::fixy::grant::ctrl;

// A cv-qualified ctrl grant tag must be rejected by IsGrantTag.
static_assert(gr::IsGrantTag<const ctrl::builtin_trap_ok>,
              "cv-qualified ctrl grant tag must be rejected by IsGrantTag");

int main() { return 0; }
