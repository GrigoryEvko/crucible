// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: storing a ScopedView under a std::unique_ptr inside a
// struct field.  Heap-allocating a View via its public copy ctor
// would otherwise escape the stack frame the mint_view call sits in.
// Tier 2 audit must recurse into std::unique_ptr.

#include <crucible/safety/ScopedView.h>

#include <memory>

struct Carrier { int v = 0; };
struct Tag     {};

constexpr bool view_ok(Carrier const&, std::type_identity<Tag>) noexcept {
    return true;
}

using View = crucible::safety::ScopedView<Carrier, Tag>;

struct Bad {
    std::unique_ptr<View> owned;  // escape via smart pointer
};

static_assert(crucible::safety::no_scoped_view_field_check<Bad>());

int main() { return 0; }
