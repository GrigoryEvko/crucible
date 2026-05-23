// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-246 HS14 fixture (Stdio 2/2) — stdio grant tags obey cv-purity.
//
// `IsGrantTag_v<G>` requires `is_same_v<G, remove_cvref_t<G>>`
// (fixy-A4-033).  A cv-qualified stdio grant tag (here
// `const write<streams::Stderr>`) is not a valid grant tag — exercises
// the shared grant_base purity gate from the stdio-family side.
//
// Mismatch class: IsGrantTag CV-PURITY rejection.
//
// Expected diagnostic: the static_assert message below (IsGrantTag).

#include <crucible/fixy/grant/Stdio.h>

namespace gr  = crucible::fixy::grant;
namespace sio = crucible::fixy::grant::stdio;

static_assert(gr::IsGrantTag<const sio::write<sio::streams::Stderr>>,
              "cv-qualified stdio grant tag must be rejected by IsGrantTag");

int main() { return 0; }
