// The compiler builds the result of build_fixed_array in the variable that the
// call initializes.  A body that captures that variable could write it beside
// the ViewMut of the frame, so the door refuses the body.

#include <foundation/core/Array.h>

int main() {
    ::foundation::core::FixedArray<int, 4> built =
        ::foundation::core::build_fixed_array<int, 4>([&built](auto run) noexcept {
            for (int& element : run) {
                built.fill(2);
                element = 1;
            }
        });
    return built.at(::foundation::core::index<0>());
}
