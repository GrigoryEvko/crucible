// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-H03-AUDIT-2: negative-compile witness for ComputationGraded's
// non-Row R rejection.
//
// Violation: instantiating ComputationGraded<NonRowType, T> where
// NonRowType is not `Row<Es...>`.  effect_row_to_at_t<NonRowType>
// resolves through detail::effect_row_to_at — a primary template
// that is INTENTIONALLY UNDEFINED (only the `Row<Es...>` partial
// specialization is defined).
//
// Locks the helper's design into CI: a future refactor that
// accidentally adds a primary-template definition (with some
// fallback like `using type = void;`) would silently produce
// a wrong-grade Graded instantiation.  This test forces such a
// refactor to update CI in lockstep.
//
// Expected diagnostic: "incomplete type" / "no member named 'type'
// in 'detail::effect_row_to_at" / "is incomplete and cannot be
// completed".

#include <crucible/effects/ComputationGraded.h>

namespace eff = crucible::effects;

// A type that is NOT Row<Es...>.  Empty struct keeps the diagnostic
// surface narrow — the issue isn't the struct's content, it's the
// missing partial specialization.
struct NotARow {};

int main() {
    // Direct alias instantiation — fires inside effect_row_to_at_t's
    // resolution at the point we ask for ::type from a primary
    // template that has no definition.
    using BadComp = eff::ComputationGraded<NotARow, int>;

    // Force completion so the compiler's lazy specialization actually
    // walks into effect_row_to_at<NotARow>::type.  An object of an
    // incomplete type cannot be constructed.
    BadComp bad{};
    (void)bad;

    return 0;
}
