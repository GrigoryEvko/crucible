// An argument of the value door build_at of a FixedArray is a reference to
// the element that the door builds.  The new element takes its storage while
// the function runs, so the function would read a part of the element that
// the door builds.  The door refuses each argument that names the array, an
// element of it or a borrow of one of them.

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
    slots.build_at(
        ::foundation::core::index<0>(),
        [](Slot const& old) noexcept { return Slot{.value = old.twice, .twice = old.value}; },
        slots.at(::foundation::core::index<0>()));
    return slots.at(::foundation::core::index<0>()).value;
}
