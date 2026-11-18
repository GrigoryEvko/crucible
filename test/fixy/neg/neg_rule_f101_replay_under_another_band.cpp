// F101 through a band of another lattice: a replay claim under a Vendor
// band x FP reassociation permitted.
//
// The stack puts Vendor outside DetSafe, against the canonical order.  The
// DetSafe band at Pure still claims the same bits on every replay, and
// reassociation reorders the sum, so the claim fails.  A band grades the
// payload and does not change it, so the rule reads the claim through
// Vendor.

#include <fixy/Bands.h>
#include <fixy/Fn.h>

int main() {
    // The size instantiates the class, and a band payload has no default
    // constructor.  So the size and not a default binding is the use, and
    // the refusal of the class body is the only error.
    static_cast<void>(sizeof(
        ::fixy::fn<::fixy::Vendor<::fixy::VendorBackend_v::NV, ::fixy::DetSafe<::fixy::DetSafeTier_v::Pure, int>>,
                   ::fixy::atom::fp::mode<::fixy::atom::fp::FpReassociate::UnrestrictedRewrite>>));
    return 0;
}
