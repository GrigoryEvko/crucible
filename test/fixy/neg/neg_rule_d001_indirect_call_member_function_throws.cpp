// D001: an indirect call through a const member function pointer that is
// not noexcept.
//
// The second mismatch class the rule reads: a pointer to a member states
// its signature as a free function pointer does, and a missing noexcept
// on it is the same hazard.  The pack trips D001 alone.

#include <fixy/Fn.h>

// The class has external linkage.  An atom whose argument has internal
// linkage has no stable identity, and the gate refuses it at tier 2.
namespace fixture {
struct callback_owner final {};
}  // namespace fixture

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::dispatch::indirect_call<void (fixture::callback_owner::*)() const>>
        refused{};
    return 0;
}
