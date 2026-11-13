// The body of a branded scope gives a closure that captures a BrandedIndex.
// The type of a closure has no template argument, so the gate of the mint
// does not see the brand in it, and the index leaves the scope of its view.
// The rule brand-escape refuses each result of a scope mint that holds its
// own scope brand, also in a capture of a closure.

#include <foundation/core/Array.h>
#include <foundation/core/Branded.h>

namespace {

namespace core = ::foundation::core;

struct ReturnsClosure final {
    template <class Brand>
    auto operator()(core::BrandedView<int, Brand> view) const noexcept {
        core::BrandedIndex<Brand> const index = view.index(0).expect("the scope holds two elements");
        return [index]() noexcept { return index.raw(); };
    }
};

}  // namespace

int main() {
    core::FixedArray<int, 2> narrow{};
    auto const leaked = core::mint_branded_scope(core::View<int>{narrow.view()}, ReturnsClosure{});
    return static_cast<int>(leaked());
}
