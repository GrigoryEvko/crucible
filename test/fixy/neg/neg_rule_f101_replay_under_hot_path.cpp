// F101 through a band of another lattice: a replay claim under HotPath x
// FP reassociation permitted.
//
// The canonical order puts HotPath outside DetSafe.  The DetSafe band at
// Pure still claims the same bits on every replay, and reassociation
// reorders the sum, so the claim fails.  A band grades the payload and
// does not change it, so the rule reads the claim through HotPath.

#include <fixy/Bands.h>
#include <fixy/Fn.h>

int main() {
    // The size instantiates the class, and a band payload has no default
    // constructor.  So the size and not a default binding is the use, and
    // the refusal of the class body is the only error.
    static_cast<void>(sizeof(
        ::fixy::fn<::fixy::HotPath<::fixy::HotPathTier_v::Hot, ::fixy::DetSafe<::fixy::DetSafeTier_v::Pure, int>>,
                   ::fixy::atom::fp::mode<::fixy::atom::fp::FpReassociate::UnrestrictedRewrite>>));
    return 0;
}
