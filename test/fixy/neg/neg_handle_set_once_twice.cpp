// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A SetOnce slot takes one pointer.  A second set would overwrite the
// first while its readers still hold it, so the precondition of set
// refuses a slot that is already set.  set is constexpr, so a constant
// evaluation reaches the check and the refusal is a compile error.
//
// Expected diagnostic: the static_assert is not a constant expression,
// because the precondition of SetOnce::set fails on the second call.

#include <fixy/handle/Once.h>

namespace h = fixy::handle;

namespace {
constexpr bool set_twice() {
    int first = 1;
    int second = 2;
    h::SetOnce<int> slot{};
    slot.set(&first);
    slot.set(&second);
    return slot.get() == &second;
}
}  // namespace

static_assert(set_twice());

int main() { return 0; }
