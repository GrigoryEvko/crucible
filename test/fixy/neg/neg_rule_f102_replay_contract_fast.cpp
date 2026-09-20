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
    [[maybe_unused]] ::fixy::fn<::fixy::DetSafe<::fixy::DetSafeTier_v::Pure, int>,
                                ::fixy::atom::fp::mode<::fixy::atom::fp::FpContract::Fast>>
        refused{};
    return 0;
}
