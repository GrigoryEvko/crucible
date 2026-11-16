// The body of a strided scope gives a closure that holds an index of the
// scope in a capture.  The type of the closure names no brand in its template
// arguments, so the gate of the mint does not see the index.  The rule
// brand-escape refuses each result of a scope mint that holds its own brand.

#include <foundation/core/Array.h>
#include <foundation/core/Strided.h>

#include <cstddef>

namespace {

namespace core = ::foundation::core;

struct ReturnsClosure final {
    template <class Brand>
    auto operator()(core::BrandedStridedView<int, 1, Brand> view) const noexcept {
        core::StridedIndex<1, Brand> const last = view.index(std::size_t{3}).expect("3 is below 4");
        return [last]() noexcept { return last.coordinates().size(); };
    }
};

}  // namespace

int main() {
    core::FixedArray<int, 4> values{};
    core::FixedArray<std::size_t, 1> const sizes{4};
    core::FixedArray<std::size_t, 1> const strides{1};
    auto const line = core::mint_strided_view(values.view(), sizes.view(), strides.view(), 0).expect("fits");
    auto const keeper = core::mint_strided_scope(line, ReturnsClosure{});
    return static_cast<int>(keeper());
}
