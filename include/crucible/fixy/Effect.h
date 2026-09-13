#pragma once

#include <crucible/effects/Computation.h>

#include <type_traits>

namespace crucible::fixy::effect {

template <class C>
concept RowEngagementWitnessed =
    ::crucible::effects::IsComputation<std::remove_cvref_t<C>> && (std::remove_cvref_t<C>::effect_count_in_row() > 0);

}  // namespace crucible::fixy::effect

namespace crucible::fixy::effect::self_test {

namespace eff = ::crucible::effects;

static_assert(!RowEngagementWitnessed<int>, "`int` is not a Computation — "
                                            "RowEngagementWitnessed must reject via IsComputation.");

static_assert(!RowEngagementWitnessed<eff::Row<eff::Effect::Bg>>,
              "a bare Row is not a Computation — "
              "RowEngagementWitnessed must reject the wrong-carrier shape.");

static_assert(!RowEngagementWitnessed<eff::Computation<eff::Row<>, int>>,
              "Computation<Row<>, int> is pure (empty row) — "
              "RowEngagementWitnessed must reject the no-engagement case.");

static_assert(!RowEngagementWitnessed<eff::Computation<eff::Row<>, int>&>,
              "cvref-stripped lvalue empty-row still rejects.");

static_assert(!RowEngagementWitnessed<const eff::Computation<eff::Row<>, int>&>,
              "cvref-stripped const-lvalue empty-row still rejects.");

static_assert(RowEngagementWitnessed<eff::Computation<eff::Row<eff::Effect::Bg>, int>>,
              "Computation<Row<Bg>, int> has a non-empty row — "
              "RowEngagementWitnessed must accept the engaged case.");

static_assert(
    RowEngagementWitnessed<eff::Computation<eff::Row<eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::IO>, double>>,
    "multi-cap Computation accepted by the concept.");

static_assert(
    [] consteval {
        auto bg = eff::Computation<eff::Row<>, int>::template lift<eff::Effect::Bg>(7);
        return RowEngagementWitnessed<decltype(bg)>;
    }(),
    "lift<Bg>(x) produces a row-engaged "
    "Computation — RowEngagementWitnessed must accept the lift result.");

static_assert(
    [] consteval {
        auto bg = eff::Computation<eff::Row<>, int>::template lift<eff::Effect::Bg>(10);
        auto chained =
            bg.then([](int x) { return eff::Computation<eff::Row<>, int>::template lift<eff::Effect::IO>(x + 1); });
        return RowEngagementWitnessed<decltype(chained)>;
    }(),
    "then-chained Computation carries the row-union. "
    "RowEngagementWitnessed must accept the engaged result.");

static_assert(
    [] consteval {
        int pure_source = 42;
        auto forged_engaged = eff::Computation<eff::Row<>, int>::template lift<eff::Effect::Bg>(pure_source);
        return RowEngagementWitnessed<decltype(forged_engaged)>;
    }(),
    "RowEngagementWitnessed is structurally honest but discipline-blind. "
    "It accepts lift<Bg>(pure_value) because the result type claims "
    "Row<Bg>, whatever discipline produced the value. A substrate-level "
    "discipline gate would make this pin red.");

inline constexpr std::size_t effect_surface_cardinality = 1;
static_assert(effect_surface_cardinality == 1, "fixy::effect:: ships exactly one concept today "
                                               "(RowEngagementWitnessed). A sibling concept must bump this "
                                               "constant.");

}  // namespace crucible::fixy::effect::self_test
