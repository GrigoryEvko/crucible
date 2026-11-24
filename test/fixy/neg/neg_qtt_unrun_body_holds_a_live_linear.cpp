// A body that no door called captures a live Linear<T> by value.  The door
// end_unrun_body would end the body, and the wrapper would end with its
// obligation open in the frame of the door.  A Linear owes a consume at its
// end, so it states no end of its own, and the deleted twin of the door
// refuses the body.

#include <fixy/Qtt.h>

#include <foundation/Linear.h>

#include <utility>

int main() {
    auto body = [held = fixy::mint_linear<int>(7)]() mutable noexcept { return std::move(held).consume(); };
    ::foundation::linear::end_unrun_body(std::move(body), ::foundation::linear::UnrunReason::refusal_test);
    return 0;
}
