// Two references to one element of a FixedArray with no key.  A closure in the
// body of the first element door opens the second one, so no rule reads it as
// an arm.  The compiler could then read a stale value of the element.  An array
// with no key lends no element that a body writes.

#include <foundation/core/Array.h>

namespace core = ::foundation::core;

namespace {

// Opens Depth element doors of element 0, each in a closure of the body of the
// one before, and writes the element after the closure.
template <int Depth>
void write_element(core::FixedArray<int, 4>& array) noexcept {
    array.with_element(core::index<0>(), [&array](int& element) noexcept -> void {
        element = Depth;
        auto const write_again = [&array]() noexcept {
            if constexpr (Depth > 1) write_element<Depth - 1>(array);
        };
        write_again();
    });
}

}  // namespace

int main() {
    core::FixedArray<int, 4> array{};
    write_element<2>(array);
    return array.at(core::index<0>());
}
