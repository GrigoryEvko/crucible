// A static local of the generic body of a strided scope keeps an index of one
// call.  One call site of mint_strided_scope gives each of its calls one
// brand, so the second call reads an index of a view of eight positions in a
// view of two, where at() does no check.  The rule brand-escape refuses each
// object of static or thread storage duration that holds a scope brand.

#include <foundation/core/Array.h>
#include <foundation/core/Strided.h>

#include <cstddef>

namespace {

namespace core = ::foundation::core;

struct KeepsLastIndex final {
    template <class Brand>
    int operator()(core::BrandedStridedView<int, 1, Brand> view) const noexcept {
        static core::Option<core::StridedIndex<1, Brand>> const kept = view.index(std::size_t{7});
        core::Option<core::StridedIndex<1, Brand>> read = kept;
        core::StridedIndex<1, Brand> const last = static_cast<decltype(read)&&>(read).expect("kept by the first scope");
        return view.at(last);
    }
};

int scope_of(core::StridedView<int, 1> view) { return core::mint_strided_scope(view, KeepsLastIndex{}); }

}  // namespace

int main() {
    core::FixedArray<int, 8> values{};
    core::FixedArray<std::size_t, 1> const wide{8};
    core::FixedArray<std::size_t, 1> const narrow{2};
    core::FixedArray<std::size_t, 1> const strides{1};
    int const first = scope_of(core::mint_strided_view(values.view(), wide.view(), strides.view(), 0).expect("fits"));
    return first + scope_of(core::mint_strided_view(values.view(), narrow.view(), strides.view(), 0).expect("fits"));
}
