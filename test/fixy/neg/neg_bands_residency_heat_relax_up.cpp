// relax moves a value down the residency chain and never up.  A Warm
// value asked to relax to Hot would claim that its working set sits in
// L1.  Only a value built where the bytes really are resident at that
// level can hold that claim.  The leq gate in the requires clause of relax
// evaluates to false for the pair, and no overload is viable.

#include <fixy/Bands.h>

int main() {
    auto warm = fixy::mint_band<fixy::residency_heat::Warm<int>>(42);
    auto hot = fixy::relax<fixy::ResidencyHeatTag_v::Hot>(warm);
    return hot.peek();
}
