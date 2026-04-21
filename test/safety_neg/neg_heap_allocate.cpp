// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: attempting to heap-allocate a ScopedView via `new`,
// bypassing the stack-only lifetime discipline.  The class-level
// `operator new = delete` (Tier 1) blocks the allocation at the
// point of the `new` expression.

#include <crucible/safety/ScopedView.h>

struct Carrier { int v = 0; };
struct Tag     {};

constexpr bool view_ok(Carrier const&, std::type_identity<Tag>) noexcept {
    return true;
}

using View = crucible::safety::ScopedView<Carrier, Tag>;

int main() {
    Carrier c;
    auto v = crucible::safety::mint_view<Tag>(c);
    // `new View(v)` would copy-construct on the heap and return a
    // pointer that can escape this stack frame.  operator new is
    // deleted, so this line must fail to compile.
    auto* p = new View(v);
    (void)p;
    return 0;
}
