// The compile-time checks of foundation/effects/Lift.h.

#include <foundation/effects/Lift.h>

namespace foundation::effects {

namespace detail::lift_self_test {

struct lifts_to_io_block {
    static constexpr auto lifts_to = Row<Effect::IO, Effect::Block>{};
};

struct lifts_to_nothing {
    static constexpr auto lifts_to = Row<>{};
};

struct no_lift {};

static_assert(LiftsToRow<lifts_to_io_block>);
static_assert(LiftsToRow<lifts_to_nothing>);
static_assert(!LiftsToRow<no_lift>);
static_assert(!LiftsToRow<int>);

static_assert(std::is_same_v<lift_row_t<lifts_to_io_block>, Row<Effect::IO, Effect::Block>>);
static_assert(std::is_same_v<lift_row_t<lifts_to_nothing>, Row<>>);

// A three-tier sample enum with a total map, and the walk that proves
// it total.
enum class SampleTier : unsigned char {
    Pure = 0,
    Reads = 1,
    Writes = 2,
};

template <SampleTier T>
struct sample_row;

template <>
struct sample_row<SampleTier::Pure> {
    using type = Row<>;
};
template <>
struct sample_row<SampleTier::Reads> {
    using type = Row<Effect::IO>;
};
template <>
struct sample_row<SampleTier::Writes> {
    using type = Row<Effect::IO, Effect::Block>;
};

static_assert(every_enumerator_lifted<SampleTier, sample_row>());

// A map whose one arm is not a row makes the walk return false.
template <SampleTier T>
struct sample_non_row {
    using type = Row<>;
};
template <>
struct sample_non_row<SampleTier::Writes> {
    using type = int;
};

static_assert(!every_enumerator_lifted<SampleTier, sample_non_row>());

}  // namespace detail::lift_self_test

}  // namespace foundation::effects
