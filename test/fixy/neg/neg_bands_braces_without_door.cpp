// A band's tier is a claim about the bytes, made at mint_band.  Braces
// that pair any value with the pinned tier name no authority, so the
// substrate has no constructor for them: a value read from rand() does
// not become Pure by being written next to the tier.

#include <fixy/Bands.h>

#include <cstdlib>

int main() {
    fixy::det_safe::Pure<int> const pure{std::rand(), {}};
    return pure.peek();
}
