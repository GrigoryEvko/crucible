// The mint of a sole writer gives a fresh brand.  A caller that names the
// brand of a token that it holds would mint a second writer of the same
// cell, so the mint refuses a second template argument.

#include <foundation/Brand.h>
#include <foundation/Linear.h>
#include <foundation/core/Atomic.h>

#include <utility>

namespace {
struct HeadRole {};
}  // namespace

int main() {
    auto first = ::foundation::core::mint_sole_writer<HeadRole>();
    using HeldBrand = ::foundation::brand::brand_of_t<decltype(first)>;
    auto second = ::foundation::core::mint_sole_writer<HeadRole, HeldBrand>();
    ::foundation::linear::drop(std::move(first));
    ::foundation::linear::drop(std::move(second));
    return 0;
}
