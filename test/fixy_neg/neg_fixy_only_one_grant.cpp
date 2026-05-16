// NEGATIVE-COMPILE TEST.  MUST FAIL TO COMPILE.
// FIXY-A6 — only one grant tag present (grant::copy).  Usage IS
// engaged, but the OTHER 19 dims are not.  IsAccepted's first
// unengaged dim is dim::Type (enumerator 0), so expected diag is
// FixyNotEngaged_Type.

#include <crucible/fixy/Reject.h>

namespace cf = crucible::fixy;
namespace cg = crucible::fixy::grant;

static_assert(cf::IsAccepted<cg::copy>, "FixyNotEngaged_Type");

int main() { return 0; }
