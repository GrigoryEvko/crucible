// E044: constant time x a suspension written on ControlFlow.
//
// The second mismatch class: ctrl::coroutine<Policy> states the same
// suspension on the axis of the ways a frame is left.  The rule reads
// both axes, so no pack avoids it by a write on the other one.  The pack
// trips E044 alone.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::constant_time,
                                ::fixy::atom::ctrl::coroutine<::fixy::atom::ctrl::async_task>> refused{};
    return 0;
}
