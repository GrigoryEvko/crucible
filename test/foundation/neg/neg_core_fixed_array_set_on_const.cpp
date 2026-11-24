// A const array has no writer of its elements, so a value door takes an array
// that is not const.

#include <foundation/core/Array.h>

namespace {

int write_through_const(::foundation::core::FixedArray<int, 4> const& array) noexcept {
    array.set(::foundation::core::index<0>(), 2);
    return array.at(::foundation::core::index<0>());
}

}  // namespace

int main() { return write_through_const(::foundation::core::FixedArray<int, 4>{1, 2, 3, 4}); }
