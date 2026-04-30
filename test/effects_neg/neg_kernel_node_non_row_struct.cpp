// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-J01-AUDIT round 2: KernelNode<UserStruct> rejection witness.
//
// Most adversarial case: a user-defined struct that LOOKS row-shaped
// (templated, has size constant, sits in the same effect space) but
// is NOT crucible::effects::Row<Es...>.  The partial specialization
// must reject this instead of fuzzy-matching on structural traits.
//
// Expected diagnostic: "invalid use of incomplete type" / "incomplete
// type".

#include <crucible/forge/KernelNode.h>

namespace fake {

// Imposter row: same surface (template parameter pack, size constant)
// but distinct type.  A correctly-engineered KernelNode rejects it.
template <int... Atoms>
struct LooksLikeRow {
    static constexpr int size = sizeof...(Atoms);
};

}  // namespace fake

int main() {
    using ImposterRow   = fake::LooksLikeRow<1, 2, 3>;
    using BadKernelNode = ::crucible::forge::KernelNode<ImposterRow>;
    static_assert(sizeof(BadKernelNode) > 0);
    return 0;
}
