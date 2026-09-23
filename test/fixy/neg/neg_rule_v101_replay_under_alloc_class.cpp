// V101 through a band of another lattice: a replay claim under AllocClass
// x a pinned SIMD ISA.
//
// The DetSafe band at Pure sits under an Arena band.  A body emitted for
// one vector ISA runs a different reduction order on a host without that
// ISA, so the replay claim does not survive a change of host.  The rule
// reads the claim through the Arena band.

#include <fixy/Bands.h>
#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<::fixy::alloc_class::Arena<::fixy::DetSafe<::fixy::DetSafeTier_v::Pure, int>>,
                                ::fixy::atom::simd::avx2>
        refused{};
    return 0;
}
