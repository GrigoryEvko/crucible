// D001: an indirect call through a ref-qualified member function pointer
// that is not noexcept.
//
// The member function carries a qualifier on its object, here an lvalue
// ref-qualifier.  The rule reads the function type by reflection, so the
// qualifier does not hide the missing noexcept.  The pack trips D001
// alone.

#include <fixy/Fn.h>

namespace {
struct callback_owner final {};
}  // namespace

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::dispatch::indirect_call<void (callback_owner::*)() &>> refused{};
    return 0;
}
