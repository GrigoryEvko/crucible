// The compiler builds the result of build_fixed_array in the member that the
// call initializes.  A body that reaches the object of that member through a
// pointer could write the array beside the ViewMut of the frame, so the door
// refuses the body.

#include <foundation/core/Array.h>

namespace {

class Holder final {
public:
    Holder() noexcept {
        ::foundation::core::build_fixed_array<int, 4>([this](auto run) noexcept {
            for (int& element : run) {
                parts_.fill(2);
                element = 1;
            }
        });
    }

    [[nodiscard]] int first() const noexcept { return parts_.at(::foundation::core::index<0>()); }

private:
    ::foundation::core::FixedArray<int, 4> parts_{};
};

}  // namespace

int main() { return Holder{}.first(); }
