// A static local of the generic body of a branded scope keeps a BrandedIndex
// of one call.  One call site of mint_branded_scope gives each of its calls
// one brand, so the second call reads an index of a scope of eight elements
// in a scope of two, where at() does no check.  The rule brand-escape refuses
// each object of static or thread storage duration that holds a scope brand.

#include <foundation/core/Array.h>
#include <foundation/core/Branded.h>

namespace {

namespace core = ::foundation::core;

struct KeepsLastIndex final {
    template <class Brand>
    int operator()(core::BrandedView<int, Brand> view) const noexcept {
        static core::Option<core::BrandedIndex<Brand>> const kept = view.index(view.size() - 1);
        core::Option<core::BrandedIndex<Brand>> read = kept;
        core::BrandedIndex<Brand> const last = static_cast<decltype(read)&&>(read).expect("kept by the first scope");
        return view.at(last);
    }
};

int scope_of(core::View<int> view) { return core::mint_branded_scope(view, KeepsLastIndex{}); }

}  // namespace

int main() {
    core::FixedArray<int, 8> wide{};
    core::FixedArray<int, 2> narrow{};
    int const first = scope_of(core::View<int>{wide.view()});
    return first + scope_of(core::View<int>{narrow.view()});
}
