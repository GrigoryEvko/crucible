// F103: constant time x an unrestricted rewrite, beside a contraction
// across statements.
//
// The second mismatch class: the rewrite comes with a second relaxation
// of the same mode.  F102 reads that contraction against a replay claim,
// and an int payload claims no replay, so F102 stands down.  The mode
// names both flushes, and the pack trips F103 alone.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<
        int, ::fixy::atom::constant_time,
        ::fixy::atom::fp::mode<::fixy::atom::fp::FpReassociate::UnrestrictedRewrite, ::fixy::atom::fp::FpContract::Fast,
                               ::fixy::atom::fp::FpDenormalInput::DenormalsAreZero,
                               ::fixy::atom::fp::FpFtz::FlushToZero>> refused{};
    return 0;
}
