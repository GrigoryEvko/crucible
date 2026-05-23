// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-245 HS14 fixture 3/3 — dispatch grant tags obey grant cv-purity.
//
// `IsGrantTag_v<G>` requires `is_same_v<G, remove_cvref_t<G>>` (closed by
// fixy-A4-033): a cv-qualified grant can only be PRODUCED by copy-pasting
// a runtime variable's `decltype` (`const auto g = tail_call{};`), a code
// smell the type system flags rather than coerces.  The V-245 dispatch
// grant tags inherit that discipline through `grant_base`; a
// `const grant::dispatch::tail_call` is NOT a valid grant tag.
//
// Mismatch class for HS14 audit: IsGrantTag CV-PURITY rejection —
// distinct from the recurses missing-bound arity path (fixture 1) and the
// virtual_call value-where-type path (fixture 2).  Exercises the shared
// grant-tag purity gate from the dispatch-family side.
//
// Expected diagnostic: the static_assert message below (IsGrantTag).

#include <crucible/fixy/grant/Dispatch.h>

namespace gr   = crucible::fixy::grant;
namespace disp = crucible::fixy::grant::dispatch;

// A cv-qualified dispatch grant tag must be rejected by IsGrantTag.
static_assert(gr::IsGrantTag<const disp::tail_call>,
              "cv-qualified dispatch grant tag must be rejected by IsGrantTag");

int main() { return 0; }
