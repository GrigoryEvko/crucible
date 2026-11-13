// A linear view of one carrier must not carry the brand of a linear view of a
// different carrier.  mint_linear_view takes its brand from its call site,
// and an explicit template argument after the tag lands in a pack that must
// be empty, so no caller spells a brand that it holds.

#include <fixy/ScopedView.h>

#include <utility>

namespace fd = ::fixy::detail;

int main() {
    fd::sv_test_carrier first_carrier{};
    fd::sv_test_carrier second_carrier{};
    auto first = ::fixy::mint_linear_view<fd::sv_test_tag>(first_carrier);
    using HeldBrand = decltype(first)::value_type::brand_type;
    auto second = ::fixy::mint_linear_view<fd::sv_test_tag, fd::sv_test_carrier, HeldBrand>(second_carrier);
    (void)std::move(first).consume();
    (void)std::move(second).consume();
    return 0;
}
