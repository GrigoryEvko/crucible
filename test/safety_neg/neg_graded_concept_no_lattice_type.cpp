// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: a wrapper publicly exposes graded_type but NOT
// lattice_type.  The strengthened GradedWrapper concept (algebra/
// GradedTrait.h, gap C2) rejects this via the
// `typename W::lattice_type` clause in the requires-expression.
//
// Before C2 strengthening, SharedPermission silently violated this
// (it only exposed value_type + graded_type, no lattice_type).
// MIGRATE-7 + GRADED-CONCEPT-C4 added the alias; this neg-compile
// guards against regression where a future wrapper forgets it.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/GradedTrait.h>
#include <crucible/algebra/lattices/QttSemiring.h>

#include <string_view>

struct MissingLatticeType {
    using value_type  = int;
    // No `using lattice_type = ...;` — DELIBERATELY ABSENT.
    using graded_type = ::crucible::algebra::Graded<
        ::crucible::algebra::ModalityKind::Absolute,
        ::crucible::algebra::lattices::QttSemiring::At<
            ::crucible::algebra::lattices::QttGrade::One>,
        int>;
    static consteval std::string_view value_type_name() noexcept { return "x"; }
    static consteval std::string_view lattice_name()    noexcept { return "y"; }
};

int main() {
    // Concept REJECTS MissingLatticeType — it satisfies graded_type-
    // is-Graded<...> but lacks the lattice_type alias the family
    // standardizes on.
    static_assert(::crucible::algebra::GradedWrapper<MissingLatticeType>);
    return 0;
}
