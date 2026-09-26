// mint_band names the band it builds exactly, so the tier a site asserts
// is spelled at that site.  A const-qualified band is not that spelling.

#include <fixy/Bands.h>

int main() {
    auto const pure = fixy::mint_band<fixy::det_safe::Pure<int> const>(7);
    return pure.peek();
}
