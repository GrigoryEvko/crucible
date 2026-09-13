#pragma once
// A foundational header reaches the Refined family through this header
// rather than through the wrapper umbrella. The umbrella re-exports every
// safety wrapper, and one of them transitively drags in the arena and its
// lattice, which a near-universal header must not carry.

#include <crucible/safety/Refined.h>
#include <crucible/safety/RefinedAlgebra.h>

#include <type_traits>

namespace crucible::fixy::wrap {

using ::crucible::safety::Refined;
using ::crucible::safety::NonNull;
using ::crucible::safety::Positive;
using ::crucible::safety::NonNegative;
using ::crucible::safety::PowerOfTwo;
using ::crucible::safety::NonZero;
using ::crucible::safety::NonEmpty;
using ::crucible::safety::NonEmptySpan;
using ::crucible::safety::MinLength;
using ::crucible::safety::MaxBounded;
using ::crucible::safety::AlignedTo;
using ::crucible::safety::WithinRange;
using ::crucible::safety::LinearRefined;
using ::crucible::safety::RefinedLinear;
using ::crucible::safety::positive;
using ::crucible::safety::non_negative;
using ::crucible::safety::non_zero;
using ::crucible::safety::is_zero;
using ::crucible::safety::non_null;
using ::crucible::safety::power_of_two;
using ::crucible::safety::non_empty;
using ::crucible::safety::all_of;
using ::crucible::safety::Aligned;
using ::crucible::safety::InRange;
using ::crucible::safety::BoundedAbove;
using ::crucible::safety::LengthGe;
using ::crucible::safety::aligned;
using ::crucible::safety::in_range;
using ::crucible::safety::bounded_above;
using ::crucible::safety::length_ge;
using ::crucible::safety::predicate_implies;
using ::crucible::safety::implies_v;

using ::crucible::safety::mint_refined;

}  // namespace crucible::fixy::wrap

namespace crucible::fixy::wrap::self_test {

static_assert(std::is_same_v<decltype(&::crucible::safety::mint_refined<::crucible::safety::positive, int>),
                             decltype(&::crucible::fixy::wrap::mint_refined<::crucible::safety::positive, int>)>,
              "mint_refined must alias the substrate factory.");

}  // namespace crucible::fixy::wrap::self_test
