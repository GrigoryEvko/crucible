// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: a wrapper exposes `graded_type = void` (or any
// non-Graded specialization).  The strengthened GradedWrapper
// concept (algebra/GradedTrait.h, gap C1) rejects this via the
// `is_graded_specialization_v<typename W::graded_type>` clause.
//
// Before C1 strengthening, the wrapper below silently passed the
// concept — the concept only checked that graded_type was a typedef,
// not that it pointed at an actual Graded specialization.  This
// test guards against regressions to the looser form.

#include <crucible/algebra/GradedTrait.h>

#include <string_view>

struct BogusGradedType {
    using value_type   = int;
    using lattice_type = int;       // (any type — concept doesn't care)
    using graded_type  = void;      // ← NOT a Graded<...> specialization
    static consteval std::string_view value_type_name() noexcept { return "x"; }
    static consteval std::string_view lattice_name()    noexcept { return "y"; }
};

int main() {
    // Concept REJECTS BogusGradedType because its graded_type isn't
    // actually a Graded<...> specialization.  static_assert fires
    // with "constraints not satisfied" naming the failed clause.
    static_assert(::crucible::algebra::GradedWrapper<BogusGradedType>);
    return 0;
}
