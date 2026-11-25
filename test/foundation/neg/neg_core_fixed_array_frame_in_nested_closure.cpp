// Two frames of one FixedArray with no key.  A closure with no parameter in the
// body of the first frame opens the second one, so no rule reads it as an arm.
// The two ViewMuts would have two brands over one run, and copy() would take a
// memcpy of two runs that overlap.  An array with no key opens no frame.

#include <foundation/core/Array.h>

#include <utility>

namespace core = ::foundation::core;

namespace {

// Opens a frame of the array.  The body of the last one copies the first 63
// elements of the run of the frame before it onto the last 63 elements.
template <class... Outer>
void shift_by_one(core::FixedArray<int, 64>& array, Outer&... outer) noexcept {
    array.with_mut([&array, &outer...](auto inner) noexcept {
        if constexpr (sizeof...(Outer) == 0) {
            [&array, &inner]() noexcept { shift_by_one(array, inner); }();
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
    shift_by_one(array);
    return array.at(core::index<63>());
}
