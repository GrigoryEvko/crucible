// The checked mint carries CRUCIBLE_PRE(Pred(v)).  A value that fails
// the predicate in a constant expression reaches the macro's consteval
// trap, so the static_assert below has a non-constant condition, and
// the expansion note names the mint.

#include <fixy/Refined.h>

namespace {

[[nodiscard]] constexpr int under_test() noexcept {
    fixy::Refined<fixy::positive, int> refused = fixy::mint_refined<fixy::positive>(-1);
    return refused.value();
}

static_assert(under_test() == -1);

}  // namespace

int main() { return 0; }
