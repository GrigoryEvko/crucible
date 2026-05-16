// NEGATIVE-COMPILE TEST.  MUST FAIL TO COMPILE.
//
// FIXY-AUDIT-NTTP — closes the NTTP-escape bug discovered during the
// FIXY-A-PLUS audit.  Pre-audit, an author could instantiate
// `accept_default_strict_for<static_cast<dim::DimAxis>(99)>` — the
// DimAxis enum class's uint8_t underlying type admits 0..255 but only
// 0..19 are real dims.  The phantom tag pointed at a non-existent
// enumerator, satisfied IsGrantTag (inherits grant_base + has a
// `relaxes` member), and silently failed to engage ANY real dim —
// masking an authoring bug as "this just doesn't pass IsAccepted".
//
// Post-audit, `accept_default_strict_for<D>` carries a static_assert
// on `dim::is_valid_axis_v<D>` so the out-of-range NTTP is a HARD
// COMPILE ERROR at the instantiation site, naming the offending
// template argument.
//
// GCC emits the diagnostic with the literal substring "outside the
// 20-enumerator range" — the PASS_REGULAR_EXPRESSION on this test
// matches.

#include <crucible/fixy/Grant.h>

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;

// Phantom DimAxis value (uint8_t admits it, enumerator set does not).
using ForgedAck = cf::accept_default_strict_for<static_cast<cd::DimAxis>(99)>;
ForgedAck token{};

int main() { return 0; }
