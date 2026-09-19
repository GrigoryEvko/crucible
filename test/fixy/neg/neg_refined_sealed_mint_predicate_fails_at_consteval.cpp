// The sealed checked mint carries the same CRUCIBLE_PRE(Pred(v)) as the
// unsealed one.  A refused value in a constant expression reaches the
// consteval trap through mint_sealed_refined.

#include <fixy/Refined.h>

namespace {

[[nodiscard]] constexpr int under_test() noexcept {
    fixy::SealedRefined<fixy::positive, int> refused = fixy::mint_sealed_refined<fixy::positive>(0);
    return refused.value();
}

static_assert(under_test() == 0);

}  // namespace

int main() { return 0; }
