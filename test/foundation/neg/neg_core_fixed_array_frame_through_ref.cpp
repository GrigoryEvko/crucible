// Two frames of one FixedArray with no key.  The body of the first frame opens
// the second one through a Ref of the array that it captures.  The two ViewMuts
// would have two brands over one run, and copy() would take a memcpy of two
// runs that overlap.  An array with no key opens no frame.

#include <foundation/core/Array.h>
#include <foundation/core/Ref.h>

#include <utility>

namespace core = ::foundation::core;

namespace {

// Opens a frame of the array that the Ref names.  The body of the last one
// copies the first 63 elements of the run of the frame before it onto the last
// 63 elements.
template <class Alias, class... Outer>
void shift_by_one(Alias const& alias, Outer&... outer) noexcept {
    (*alias).with_mut([&alias, &outer...](auto inner) noexcept {
        if constexpr (sizeof...(Outer) == 0) {
            shift_by_one(alias, inner);
        } else {
            auto [first, tail] = core::split_at<1>(std::move(inner));
            auto [head, last] = core::split_at<63>(std::move(outer...[0]));
            static_cast<void>(core::copy(std::move(tail), head).is_ok());
            static_cast<void>(first.size() + last.size());
        }
    });
}

}  // namespace

int main() {
    core::FixedArray<int, 64> array{};
    auto const alias = core::to(array);
    shift_by_one(alias);
    return array.at(core::index<63>());
}
