#pragma once

// This specialization must be visible before any translation unit
// instantiates the gate for this category. An explicit specialization
// declared after the primary template is already instantiated is
// ill-formed. Include this header next to the probe declarations, or from
// a diagnostic umbrella header that every consumer sees.
//
// It lives apart from the probe header so that consumers who never probe
// graded wrappers do not pay for the graded-trait include.

#include <crucible/safety/diag/CheatProbe.h>
#include <crucible/algebra/GradedTrait.h>

namespace crucible::safety::diag {

// GradedWrapper constrains types, so the function-pointer gate admits
// nothing.

template <>
struct concept_gate<Category::GradedWrapperViolation> {
    static constexpr bool defined = true;

    template <typename T>
    static constexpr bool admits_type = ::crucible::algebra::GradedWrapper<T>;

    template <auto FnPtr>
    static constexpr bool admits_function = false;
};

static_assert(is_gate_defined_v<Category::GradedWrapperViolation>,
              "the graded-wrapper concept gate is not marked defined");

}  // namespace crucible::safety::diag
