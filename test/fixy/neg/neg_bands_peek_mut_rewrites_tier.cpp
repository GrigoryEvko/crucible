// A write in place would keep the Pure tier over bytes that rand()
// produced.  The tier is a claim about the bytes, so the keyless mutable
// reference does not exist on a band.

#include <fixy/Bands.h>

#include <cstdlib>

int main() {
    auto pure = fixy::mint_band<fixy::det_safe::Pure<int>>(7);
    pure.peek_mut() = std::rand();
    return pure.peek();
}
