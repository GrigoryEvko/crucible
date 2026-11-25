// A frame of a FixedHeapArray with no key opens a frame of the same array in its
// body.  The two bodies would hold two ViewMuts of one run with two brands, and
// each could write it beside the other.  An array with no key opens no frame.

#include <foundation/core/FixedHeapArray.h>
#include <foundation/effects/Effect.h>

namespace core = ::foundation::core;

namespace {

// Opens Depth frames of the array, each in the body of the one before.
template <int Depth>
void open_frames(core::FixedHeapArray<int, 4>& array) noexcept {
    array.with_mut([&array](auto run) noexcept {
        if constexpr (Depth > 1) open_frames<Depth - 1>(array);
        static_cast<void>(run.size());
    });
}

}  // namespace

int main() {
    core::FixedHeapArray<int, 4> array =
        core::mint_fixed_heap_array<int, 4>(::foundation::effects::testing::test().alloc, 0);
    open_frames<2>(array);
    return array.at(core::index<0>());
}
