// Negative compile test.  This file must fail to compile.
//
// A view of a carrier whose brand is a scope brand names the brand of the
// scope, and the rule brand-escape refuses each value of that brand that
// outlives the scope.  A view on the erased identity names no brand that the
// rule could read, so the erase door of a view refuses a scope brand, as the
// door of a Ref does (neg_core_ref_erase_scope_brand).

#include <fixy/ScopedView.h>
#include <foundation/core/Array.h>
#include <foundation/core/Branded.h>
#include <foundation/core/Ref.h>

#include <type_traits>

namespace {

// The brand of a Ref, read in an unevaluated operand.
template <class Target, class Brand>
Brand brand_of(::foundation::core::Ref<Target, Brand> ref);

template <class Brand>
struct ScopeCarrier {
    using brand_type = Brand;
    bool ready = true;
};
struct Ready {};
template <class Brand>
constexpr bool view_ok(ScopeCarrier<Brand> const& c, std::type_identity<Ready>) noexcept {
    return c.ready;
}

}  // namespace

int main() {
    ::foundation::core::FixedArray<int, 2> numbers{1, 2};
    return ::foundation::core::mint_branded_scope(numbers.view(), [](auto scope) noexcept {
        int sum = 0;
        for (auto [index, element] : scope.enumerate()) {
            ScopeCarrier<decltype(brand_of(element))> const carrier{};
            auto const view = ::fixy::mint_view<Ready>(carrier);
            auto const escaped = ::fixy::erase_brand(view);
            sum += (escaped->ready ? *element : 0) + static_cast<int>(index.raw());
        }
        return sum;
    });
}
