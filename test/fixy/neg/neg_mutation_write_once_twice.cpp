// WriteOnce::set carries CRUCIBLE_PRE(!value_.has_value()).  A second
// set in a constant expression reaches the macro's consteval trap, so
// the static_assert below has a non-constant condition, and the
// expansion note names WriteOnce::set.

#include <fixy/Mutation.h>

namespace {

[[nodiscard]] constexpr int under_test() noexcept {
    fixy::WriteOnce<int> slot = fixy::mint_write_once<int>();
    slot.set(1);
    slot.set(2);
    return 0;
}

static_assert(under_test() == 0);

}  // namespace

int main() { return 0; }
