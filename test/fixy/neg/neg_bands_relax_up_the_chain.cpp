// relax moves a value down the chain and never up.  A PhiloxRng value
// asked to relax to Pure would claim a determinism its source does not
// provide, so the requires clause of relax leaves no viable overload:
// the leq gate evaluates to false for the pair.

#include <fixy/Bands.h>

int main() {
    auto philox = fixy::mint_band<fixy::det_safe::PhiloxRng<int>>(42);
    auto pure = fixy::relax<fixy::DetSafeTier_v::Pure>(philox);
    return pure.peek();
}
