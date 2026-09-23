// E044: constant time x a suspension written on Reentrancy.
//
// The scheduler decides when the body resumes, and it sees the
// classified data through that timing.  The pack names no borrow, no Bg
// row and no session, and trips E044 alone.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::constant_time, ::fixy::atom::coroutine> refused{};
    return 0;
}
