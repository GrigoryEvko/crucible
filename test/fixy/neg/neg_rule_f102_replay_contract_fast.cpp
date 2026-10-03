// F102: a replay-deterministic payload x FP contraction across statements.
//
// -ffp-contract=fast fuses a multiply and an add that sit in different
// statements into one FMA, which rounds once where the source rounds
// twice.  Which pairs get fused depends on the optimiser's view of the
// whole function, so the rounding sequence differs between builds, and a
// build is a replay.  Contraction within one expression is deliberately
// admitted: CLAUDE.md V pins -ffp-contract=on for exactly that reason.

#include <fixy/Bands.h>
#include <fixy/Fn.h>

int main() {
    // The size instantiates the class, and a band payload has no default
    // constructor.  So the size and not a default binding is the use, and
    // the refusal of the class body is the only error.
    static_cast<void>(sizeof(::fixy::fn<::fixy::DetSafe<::fixy::DetSafeTier_v::Pure, int>,
                                        ::fixy::atom::fp::mode<::fixy::atom::fp::FpContract::Fast>>));
    return 0;
}
