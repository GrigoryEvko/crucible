// Two mints of a view are two brands, so a callee that asks for two
// views of one minting refuses views minted at two sites by deduction.
// This is the identity refusal for the view; the sibling fixture
// neg_brand_scoped_view_from_temporary_carrier.cpp is the refusal by
// value category, and the two are different mechanisms.

#include <fixy/ScopedView.h>

#include <type_traits>

namespace {
struct Carrier {
    bool ready = true;
};
struct Ready {};
constexpr bool view_ok(Carrier const& c, std::type_identity<Ready>) noexcept { return c.ready; }

template <class Brand>
constexpr void same_minting(::fixy::ScopedView<Carrier, Ready, Brand>,
                            ::fixy::ScopedView<Carrier, Ready, Brand>) noexcept {}
}  // namespace

int main() {
    Carrier carrier{};
    auto first = ::fixy::mint_view<Ready>(carrier);
    auto second = ::fixy::mint_view<Ready>(carrier);
    same_minting(first, second);
    return 0;
}
