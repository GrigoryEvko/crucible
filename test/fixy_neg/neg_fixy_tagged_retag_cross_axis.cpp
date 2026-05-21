// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-025 fixture #2: `Tagged<T, ft::source::External>::
// retag<ft::trust::Verified>()` must reject the cross-axis transition
// when both tags are reached via the fixy::tags re-export.
//
// Violation: invoking `Tagged::retag<NewTag>()` for a cross-axis
// phantom transition (source::* → trust::*) where both `Tag` and
// `NewTag` are spelled through `fixy::tags::*`.  The V-024 wired-in
// requires-clause `requires RetagAllowed<Tag, NewTag>` consults the
// V-023 catalog — cross-axis transitions are NEVER admitted because
// laundering provenance into verification status is meaningless
// across axes — and rejects the call.
//
// Sister fixture to neg_fixy_retag_policy_default_rejects.cpp.  That
// one hits the GATE level via a free function template constrained
// on the fixy alias.  This one hits the CONSUMER level: the V-024
// wired requires-clause on the production `Tagged::retag()`,
// invoked with both tags spelled through the fixy band.  Distinct
// mismatch class (cross-axis catalog miss vs sentinel pair) per HS14
// "two-distinct-mismatch-classes" discipline.

#include <crucible/fixy/Source.h>
#include <crucible/safety/Tagged.h>

namespace ft = crucible::fixy::tags;

int main() {
    // Construct a Tagged with a source::* phantom reached via the
    // fixy alias.  The substrate type is `safety::source::External`;
    // the alias preserves typename identity.
    crucible::safety::Tagged<int, ft::source::External> external{42};

    // Cross-axis retag — `ft::trust::Verified` aliases
    // `safety::trust::Verified`.  V-023's catalog never admits
    // source::* → trust::*; V-024's wired requires-clause on
    // `Tagged::retag()` rejects at the call site.
    auto verified = std::move(external).retag<ft::trust::Verified>();
    (void)verified;
    return 0;
}
