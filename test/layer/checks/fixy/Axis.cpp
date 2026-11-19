// The compile-time checks of fixy/Axis.h.

#include <fixy/Axis.h>

namespace fixy {

namespace detail {

// Both answers, so a caller's walk cannot pass through a helper that
// always says yes.
static_assert(text_contains("fixy::fn<T>", "fn<"));
static_assert(text_contains("abc", "abc"));
static_assert(text_contains("abc", "a"));
static_assert(text_contains("abc", "c"));
static_assert(text_contains("abc", ""));
static_assert(!text_contains("abc", "abcd"));
static_assert(!text_contains("abc", "x"));
static_assert(!text_contains("", "a"));

}  // namespace detail

static_assert(every_axis_has_traits(),
              "fixy::Axis: an axis has no row in axis_record, or its row does not classify as exactly "
              "one of caller-supplied, strict or derived.  An axis whose grade is a Fact lattice "
              "discharged at the type level, with no wrapper, takes the default row and needs nothing "
              "else.  Any other axis needs a row of its own next to the others.");

static_assert(every_pole_is_the_weakest_claim(), detail::weakest_claim_diagnostic_());

// The check has the two answers, so the walk above cannot pass through a
// rule that always says yes.
static_assert(PoleFitsClaim<Claim::Fact, pole::Unconstrained<Axis::Lifetime>>);
static_assert(PoleFitsClaim<Claim::Fact, tags::trust::Unverified>);
static_assert(PoleFitsClaim<Claim::Right, ::foundation::effects::Row<>>);
static_assert(PoleFitsClaim<Claim::Right, pole::stale::Fresh>);
static_assert(!PoleFitsClaim<Claim::Right, pole::Unconstrained<Axis::Effect>>,
              "on a Right axis a pole that claims nothing would grant every right");
static_assert(!PoleFitsClaim<Claim::Fact, tags::source::FromInternal>,
              "on a Fact axis a pole that names a source would give every binding that source");
static_assert(!PoleFitsClaim<Claim::Fact, std::integral_constant<std::uint32_t, 1u>>,
              "on a Fact axis a pole that names a version would give every binding that version");

// The walk above rejects an axis only if the check it rests on can answer
// no.  A value one past the enum stands in for a value that no case of
// axis_record names.  Without this line the check could pass by being
// vacuous.
static_assert(!AxisIsClassified<static_cast<Axis>(axis_count)>,
              "fixy::Axis: the check must reject an axis that the table has no row for.  It answers yes "
              "for a value one past the enum, so a value with no row would get a pole that no author "
              "gave it.");
static_assert(!HasStrictPole<static_cast<Axis>(axis_count)> && !TakesDefaultTraits<static_cast<Axis>(axis_count)>,
              "fixy::Axis: a value with no row has no pole and does not take the default row.");

static_assert(std::is_same_v<strict_pole_t<Axis::Observability>, strict_pole_t<Axis::Effect>>,
              "Observability's derived pole must round-trip to Effect's strict pole.");
static_assert(axis_record(Axis::Observability).derived_from == Axis::Effect && HasDerivedPole<Axis::Observability>);

static_assert(axis_count == std::meta::enumerators_of(^^Axis).size(),
              "axis_count is a literal count of the enumerators of Axis, so that no includer of the header walks "
              "the enum.  Write the new count in its initializer.");

}  // namespace fixy
