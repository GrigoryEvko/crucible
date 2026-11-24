// The gate of a lent arm asks first whether the arm takes its part by value,
// and the test substitutes the part into the call of a generic arm.  An arm
// of the form auto&& with a deduced result is then instantiated for an
// rvalue of the part, which the match never gives it.  An arm whose body
// refuses an rvalue stops the build with its own error (LentArmStoresNoBorrow
// of foundation/core/Choice.h).  The fixture pins that behavior.

#include <foundation/core/Choice.h>

#include <type_traits>

namespace {
struct Plain {
    int value = 0;
};
}  // namespace

int main() {
    ::foundation::core::Option<Plain> const held = ::foundation::core::Option<Plain>::some(Plain{3});
    int const base = 4;
    return held.match(
        [&base](auto&& payload) noexcept {
            static_assert(!std::is_rvalue_reference_v<decltype(payload)>, "the arm refuses an rvalue");
            return payload.value + base;
        },
        [] noexcept { return 0; });
}
