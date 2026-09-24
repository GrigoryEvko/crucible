// D001: an indirect call through a const member function pointer that is
// not noexcept.
//
// The second mismatch class the rule reads: a pointer to a member states
// its signature as a free function pointer does, and a missing noexcept
// on it is the same hazard.  The pack trips D001 alone.

#include <fixy/Fn.h>

namespace {
struct callback_owner final {};
}  // namespace

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::dispatch::indirect_call<void (callback_owner::*)() const>>
        refused{};
    return 0;
}
