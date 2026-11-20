// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A view of a carrier with a named brand takes the brand of the carrier, and
// it does not convert to the erased identity.  Each erased view of one
// carrier type and one state is one type, so an implicit erasure would let a
// view of one carrier stand in for a view of another.  erase_brand(view) is
// the one door, and decltype(erase_brand(view)) spells the erased view here.

#include <fixy/ScopedView.h>

#include <type_traits>

namespace {
struct NamedBrand {};
struct Carrier {
    using brand_type = NamedBrand;
    bool ready = true;
};
struct Ready {};
constexpr bool view_ok(Carrier const& c, std::type_identity<Ready>) noexcept { return c.ready; }
}  // namespace

int main() {
    Carrier carrier{};
    auto const view = ::fixy::mint_view<Ready>(carrier);
    using Erased = decltype(::fixy::erase_brand(view));
    Erased const erased = view;
    return erased->ready ? 0 : 1;
}
