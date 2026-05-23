// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-246 HS14 fixture (Stack 2/2) — stack grant tags obey cv-purity.
//
// `IsGrantTag_v<G>` requires `is_same_v<G, remove_cvref_t<G>>`
// (fixy-A4-033).  A cv-qualified stack grant tag (here `const vla_ok`) is
// not a valid grant tag — exercises the shared grant_base purity gate
// from the stack-family side.
//
// Mismatch class: IsGrantTag CV-PURITY rejection.
//
// Expected diagnostic: the static_assert message below (IsGrantTag).

#include <crucible/fixy/grant/Stack.h>

namespace gr  = crucible::fixy::grant;
namespace stk = crucible::fixy::grant::stack;

static_assert(gr::IsGrantTag<const stk::vla_ok>,
              "cv-qualified stack grant tag must be rejected by IsGrantTag");

int main() { return 0; }
