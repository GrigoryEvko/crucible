// A body of the member frame door with_mut of a FixedArray opens the door of
// the same array again.  The two bodies would hold two ViewMuts of one run,
// and each could write it beside the other.  The door lends the run in place
// and carries the annotation borrow_match, so the rule borrow-match refuses a
// call of a member of the array that is not const in the body.

#include <foundation/core/Array.h>

int main() {
    ::foundation::core::FixedArray<int, 4> array{};
    array.with_mut([&array](auto outer) noexcept {
        array.with_mut([](auto inner) noexcept { static_cast<void>(inner.size()); });
        static_cast<void>(outer.size());
    });
    return array.at(::foundation::core::index<0>());
}
