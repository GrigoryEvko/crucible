// The function of the value door build_at of a FixedArray captures a
// reference to the array.  The new element takes its storage while the
// function runs, so a read of the array from the function would read a part
// of the element that the door builds.  The door takes only a function that
// holds no state, so it sees each value that the function reads as an
// argument, and it refuses this function.

#include <foundation/core/Array.h>
#include <foundation/core/Index.h>

namespace {

struct Slot {
    int value = 0;
    int twice = 0;
};

}  // namespace

int main() {
    ::foundation::core::FixedArray<Slot, 2> slots{};
    ::foundation::core::FixedArray<Slot, 2> const& reader = slots;
    slots.build_at(
        ::foundation::core::index<0>(),
        [&reader](int value) noexcept {
            return Slot{.value = value, .twice = reader.at(::foundation::core::index<0>()).value};
        },
        3);
    return slots.at(::foundation::core::index<0>()).value;
}
