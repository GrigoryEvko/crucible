// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A reset of a SetOnce slot that was never set hides a missed
// initialization, so the precondition of reset refuses it.  reset is
// constexpr, so a constant evaluation reaches the check and the refusal
// is a compile error.
//
// Expected diagnostic: the static_assert is not a constant expression,
// because the precondition of SetOnce::reset fails on the unset slot.

#include <fixy/handle/Once.h>

namespace h = fixy::handle;

namespace {
constexpr bool reset_unset() {
    h::SetOnce<int> slot{};
    slot.reset();
    return !slot.has_value();
}
}  // namespace

static_assert(reset_unset());

int main() { return 0; }
