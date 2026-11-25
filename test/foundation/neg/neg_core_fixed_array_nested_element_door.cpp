// A body of the element door with_element of a FixedArray calls the door of
// the same array again, for the same element.  The door with no key gives an
// element that two threads can write at the same time, and it carries the
// annotation borrow_match.  So the rule borrow-match refuses a call of a member
// of the array that is not const in the body.

#include <foundation/core/Array.h>
#include <foundation/core/Atomic.h>

using Counter = ::foundation::core::Atomic<int>;

int main() {
    ::foundation::core::FixedArray<Counter, 4> array{};
    array.with_element(::foundation::core::index<0>(), [&array](Counter& first) noexcept {
        array.with_element(::foundation::core::index<0>(),
                           [&first](Counter& second) noexcept { second.store_release(first.load_acquire() + 1); });
        first.store_release(2);
    });
    return array.at(::foundation::core::index<0>()).load_acquire();
}
