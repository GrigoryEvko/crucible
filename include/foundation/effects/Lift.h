#pragma once

// The lift from a marker type to the effect row it stands for.
//
// A marker that reaches a real operation carries the row of that
// operation as `static constexpr auto lifts_to = Row<...>{}`.  This
// header reads the member structurally: `LiftsToRow` is satisfied by
// any type that declares it, and `lift_row_t` is the row's type.  The
// layer that declares the markers is above this one and is never
// named here, so a gate in that layer satisfies the concept by shape.
//
// `every_enumerator_lifted` is the exhaustiveness proof for a mapping
// from an enum to rows.  It walks the enumerators by reflection and
// instantiates the mapping at each one, so an enumerator the mapping
// forgot fails as an incomplete type inside the walk rather than as a
// missing arm nobody noticed.
//
// Old spelling: include/crucible/fixy/syscall/Bridge.h, whose
// row_for_family map and every_syscall_family_lifted walk were bound to
// one enum and one grant family.

#include <foundation/effects/Row.h>

#include <meta>
#include <type_traits>

namespace foundation::effects {

template <class Atom>
concept LiftsToRow = requires { Atom::lifts_to; };

namespace detail {

// The primary template is constrained rather than left undefined, so
// a type with no `lifts_to` fails at the constraint with its name in
// the diagnostic.  The assertion inside catches a `lifts_to` that is
// present but is not a row.
template <LiftsToRow Atom>
struct lift_row {
    using type = std::remove_cvref_t<decltype(Atom::lifts_to)>;
    static_assert(IsEffectRow<type>, "foundation/effects/Lift.h: `lifts_to` must be a value of an effects::Row "
                                     "type.  The member exists on this type but is something else.");
};

}  // namespace detail

template <LiftsToRow Atom>
using lift_row_t = typename detail::lift_row<Atom>::type;

// `Map` is a class template over the enumerators of `Enum`, and
// `Map<E>::type` must be a row for every enumerator.  A missing
// specialisation fails as an incomplete type at the alias, before the
// row check runs, and a specialisation whose `type` is not a row
// returns false.
template <class Enum, template <Enum> class Map>
    requires std::is_scoped_enum_v<Enum>
[[nodiscard]] consteval bool every_enumerator_lifted() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^Enum));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        // The alias instantiation is itself the exhaustiveness test.
        // A splice in template-argument position requires the
        // parentheses.
        using row_t = typename Map<([:en:])>::type;
        if constexpr (!IsEffectRow<row_t>) return false;
    }
#pragma GCC diagnostic pop
    return true;
}

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
