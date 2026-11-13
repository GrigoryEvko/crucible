// A view of one carrier must not carry the brand of a view of a different
// carrier, or a callee that asks for one brand takes views of two carriers.
// mint_view takes its brand from its call site, and an explicit template
// argument after the tag lands in a pack that must be empty, so no caller
// spells a brand that it holds.

#include <fixy/ScopedView.h>

namespace fd = ::fixy::detail;

int main() {
    fd::sv_test_carrier first_carrier{};
    fd::sv_test_carrier second_carrier{};
    auto first = ::fixy::mint_view<fd::sv_test_tag>(first_carrier);
    using HeldBrand = decltype(first)::brand_type;
    auto second = ::fixy::mint_view<fd::sv_test_tag, fd::sv_test_carrier, HeldBrand>(second_carrier);
    (void)first;
    (void)second;
    return 0;
}
