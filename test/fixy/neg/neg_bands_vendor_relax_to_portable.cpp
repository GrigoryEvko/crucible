// relax moves a value down the backend order and never up.  Portable is
// the top, so an NV value asked to relax to Portable would claim that it
// runs on every backend.  Its producer built it for NV only.  The leq gate
// in the requires clause of relax evaluates to false for the pair, and no
// overload is viable.

#include <fixy/Bands.h>

int main() {
    auto nv = fixy::mint_band<fixy::vendor::Nv<int>>(42);
    auto portable = fixy::relax<fixy::VendorBackend_v::Portable>(nv);
    return portable.peek();
}
