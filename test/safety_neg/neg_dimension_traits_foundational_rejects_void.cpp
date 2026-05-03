// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Phase 0 P0-6 fixture (#1099) for safety::DimensionTraits.h's
// FoundationalGrade concept (#1094).
//
// Why this matters: Tier-F dimensions (DimensionAxis::Type and
// DimensionAxis::Refinement per fixy.md §24.1) admit any complete
// object type.  The concept FoundationalGrade<G> = std::is_object_v<G>
// — broad on purpose, since dim 1 (Type) literally accepts the
// substrate's value-type universe.
//
// Even with the broad shape, the gate must still REJECT non-object
// types: void, references, bare function types.  void is the
// canonical rejection — `std::is_object_v<void>` is false because
// you cannot have an object of type void (no storage, no value,
// no member access).
//
// Without the gate, a downstream consumer that demands
// FoundationalGrade<X> would silently admit X = void / X = T& /
// X = R(Args...) and trip a downstream "void cannot be used as
// type" / "reference is not an object type" error far from the
// actual mistake.  This fixture pins the rejection at the gate
// evaluation site.
//
// Expected diagnostic: "constraints not satisfied" pointing at
// the FoundationalGrade<void> evaluation.

#include <crucible/safety/DimensionTraits.h>

namespace neg = crucible::safety;

// Bridge fires: FoundationalGrade<void> evaluates to
// std::is_object_v<void> which is false.  The concept rejects.
template <neg::FoundationalGrade G>
constexpr bool consumes_foundational() noexcept { return true; }

[[maybe_unused]] constexpr bool the_fixture =
    consumes_foundational<void>();

int main() { return 0; }
