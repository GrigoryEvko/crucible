// MUST fail to compile: one canonical layer twice in one stack.  The
// canonical order requires strictly increasing positions, so a DetSafe
// inside a DetSafe is a defect and not a reordering.  A gate that
// accepted it would let two stacks with one meaning take two cache
// slots.

#include <fixy/CanonicalOrder.h>

namespace {

namespace co = ::fixy::canonical_order;

template <co::CanonicallyOrdered Stack>
constexpr int accept_canonical_stack() {
    return 0;
}

using DuplicateStack =
    ::fixy::DetSafe<::fixy::DetSafeTier_v::Pure, ::fixy::DetSafe<::fixy::DetSafeTier_v::PhiloxRng, int>>;

}  // namespace

int main() { return accept_canonical_stack<DuplicateStack>(); }
