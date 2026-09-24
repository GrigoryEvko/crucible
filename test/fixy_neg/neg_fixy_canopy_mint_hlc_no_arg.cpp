#include <crucible/fixy/Canopy.h>

// FIXY-V-213 fixture #2: the fixy::canopy::mint_hlc re-export MUST
// require the `effects::Init` cap-tag argument — no defaulted form
// exists, so calling with zero arguments must fail compilation
// through the fixy:: surface with the same overload-resolution
// diagnostic as the bare substrate call.
//
// This fixture covers a distinct compile-error class from #1
// (parameter-type-mismatch vs no-matching-overload), satisfying the
// HS14 distinct-mismatch-class floor of two fixtures per re-exported
// mint.

int main() {
    auto&& clock = crucible::fixy::canopy::mint_hlc();
    (void)clock;
    return 0;
}
