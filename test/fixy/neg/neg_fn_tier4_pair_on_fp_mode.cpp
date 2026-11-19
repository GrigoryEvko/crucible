// Tier 4 refuses two atoms on FpMode.  FpMode is a Lattice axis in the
// table of fixy/Axis.h, so it carries one grade.  One mode atom names each
// setting of the mode, and two mode atoms do not say which mode the body
// runs under.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::fp::mode<::fixy::atom::fp::FpContract::Fast>,
                                ::fixy::atom::fp::mode<::fixy::atom::fp::FpReassociate::UnrestrictedRewrite>> refused{};
    return 0;
}
