// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule S001 (FIXY-V-243):
//
//     marks_hot_path<F>::value == true
//   ∧ F::type_t carries a StdioPinned tier >= BufferedWrite
//   ⇒ ill-formed
//
// FIXY-FOUND-069 sub-HS14 closure: S001's trigger is a tier-threshold
// (`stdio_at_or_above_v<BufferedWrite, type_t>`), and the Stdio lattice
// has THREE values at or above the threshold — BufferedWrite(1),
// UnbufferedWrite(2), InteractiveRead(3).  The sibling fixture
// neg_collision_S001_hotpath_stdio.cpp witnesses the BufferedWrite arm;
// this companion witnesses the UNBUFFEREDWRITE arm.  Distinct mismatch
// class, mirroring the P010 Alloc/Block/IO multi-arm precedent: a
// different tier value independently satisfies the same `>=` predicate,
// so a refactor that narrowed the gate from `>= BufferedWrite` to an
// exact `== BufferedWrite` would silently re-admit the strictly-worse
// UnbufferedWrite case (one flushing syscall PER call) on the hot path
// — and only this arm catches it.  S001 is the sole Stdio-axis rule, so
// no other CollisionCatalog rule cross-fires on this pack.
//
// Expected diagnostic substring: "S001:"

#include <crucible/safety/Fn.h>
#include <crucible/safety/Stdio.h>

namespace fn = crucible::safety::fn;
namespace sf = crucible::safety;
using SIO = crucible::algebra::lattices::Stdio;

namespace neg_collision_s001_unbuf {
// UnbufferedWrite (tier 2) — strictly above BufferedWrite (tier 1); the
// `>= BufferedWrite` threshold still fires, witnessing the second arm.
using Bad = fn::Fn<sf::StdioPinned<SIO::UnbufferedWrite, int>>;
}  // namespace neg_collision_s001_unbuf

// Mark Bad as hot-path — required to fire S001 (the rule guards
// marks_hot_path AND a Stdio tier >= BufferedWrite).
namespace crucible::safety::fn::collision {
    template <> struct marks_hot_path<::neg_collision_s001_unbuf::Bad>
        : std::true_type {};
}  // namespace crucible::safety::fn::collision

[[maybe_unused]] neg_collision_s001_unbuf::Bad the_fixture{};

int main() { return 0; }
