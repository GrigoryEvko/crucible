// D001: an indirect call through a free function pointer that is not
// noexcept.
//
// A throw out of the callee crosses a boundary that promised none and
// ends the process.  The family is named by the pointer type itself, so
// the signature is stated and the rule reads it.  No other rule reads
// Axis::CallShape's indirect_call, so the pack trips D001 alone.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::dispatch::indirect_call<void (*)(void*, int)>> refused{};
    return 0;
}
