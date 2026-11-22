// Negative compile test.  This file must fail to compile.
//
// A brand that names a scope brand as a type argument, such as the brand of
// a view of a walk of rows, carries the scope as the scope brand does.  The
// erase door of a view reads each depth of the template arguments of the
// brand, so it refuses the wrapped brand too, as the door of a Ref does.

#include <fixy/ScopedView.h>
#include <foundation/core/Array.h>
#include <foundation/core/Branded.h>
#include <foundation/core/Ref.h>

#include <type_traits>

namespace {

// The brand of a Ref, read in an unevaluated operand.
template <class Target, class Brand>
Brand brand_of(::foundation::core::Ref<Target, Brand> ref);

// A brand that names its inner brand as its one type argument.
template <class Inner>
struct WrapsBrand final {};

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
            ScopeCarrier<WrapsBrand<decltype(brand_of(element))>> const carrier{};
            auto const view = ::fixy::mint_view<Ready>(carrier);
            auto const escaped = ::fixy::erase_brand(view);
            sum += (escaped->ready ? *element : 0) + static_cast<int>(index.raw());
        }
        return sum;
    });
}
