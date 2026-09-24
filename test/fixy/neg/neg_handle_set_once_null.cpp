// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// SetOnce reads a null pointer as the unset state, so a set of null would
// store a value that the slot can never report back.  The precondition of
// set refuses it.  set is constexpr, so a constant evaluation reaches the
// check and the refusal is a compile error.
//
// Expected diagnostic: the static_assert is not a constant expression,
// because the precondition of SetOnce::set fails on the null pointer.

#include <fixy/handle/Once.h>

namespace h = fixy::handle;

namespace {
constexpr bool set_null() {
    h::SetOnce<int> slot{};
    slot.set(nullptr);
    return !slot.has_value();
}
}  // namespace

static_assert(set_null());

int main() { return 0; }
