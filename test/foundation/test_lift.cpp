// Sentinel TU for foundation/effects/Lift.h: a type with `lifts_to`
// satisfies LiftsToRow and its row is read off structurally, a type
// without it does not, and every_enumerator_lifted proves a map over an
// enum total and refuses one whose arm is not a row.

#include <foundation/effects/Lift.h>

#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

#include <cstdint>
#include <type_traits>

namespace {

namespace fe = ::foundation::effects;

// A marker the way the layer above declares one: the row is a value of
// the row type, and the type carries nothing else.
struct reaches_disk final {
    static constexpr auto lifts_to = fe::Row<fe::Effect::IO, fe::Effect::Block>{};
};

struct reaches_nothing final {
    static constexpr auto lifts_to = fe::Row<>{};
};

struct plain_marker final {};

static_assert(fe::LiftsToRow<reaches_disk>);
static_assert(fe::LiftsToRow<reaches_nothing>);
static_assert(!fe::LiftsToRow<plain_marker>);
static_assert(!fe::LiftsToRow<int>);
static_assert(!fe::LiftsToRow<void>);

static_assert(std::is_same_v<fe::lift_row_t<reaches_disk>, fe::Row<fe::Effect::IO, fe::Effect::Block>>);
static_assert(std::is_same_v<fe::lift_row_t<reaches_nothing>, fe::Row<>>);
static_assert(fe::row_contains_v<fe::lift_row_t<reaches_disk>, fe::Effect::Block>);
static_assert(!fe::row_contains_v<fe::lift_row_t<reaches_nothing>, fe::Effect::IO>);

// The lifted row is a canonical row, so a subrow query over it agrees
// with the row algebra.
static_assert(fe::Subrow<fe::lift_row_t<reaches_nothing>, fe::lift_row_t<reaches_disk>>);
static_assert(!fe::Subrow<fe::lift_row_t<reaches_disk>, fe::lift_row_t<reaches_nothing>>);

// A four-tier enum with a total map.
enum class Surface : std::uint8_t {
    None = 0,
    Vdso = 1,
    ReadOnly = 2,
    Mutating = 3,
};

template <Surface S>
struct surface_row;

template <>
struct surface_row<Surface::None> {
    using type = fe::Row<>;
};
template <>
struct surface_row<Surface::Vdso> {
    using type = fe::Row<>;
};
template <>
struct surface_row<Surface::ReadOnly> {
    using type = fe::Row<fe::Effect::IO>;
};
template <>
struct surface_row<Surface::Mutating> {
    using type = fe::Row<fe::Effect::IO, fe::Effect::Block>;
};

static_assert(fe::every_enumerator_lifted<Surface, surface_row>());

// The same enum with one arm that is not a row: the walk returns false
// instead of accepting the map.
template <Surface S>
struct surface_non_row {
    using type = fe::Row<>;
};
template <>
struct surface_non_row<Surface::ReadOnly> {
    using type = double;
};

static_assert(!fe::every_enumerator_lifted<Surface, surface_non_row>());

// A map that is total over a one-enumerator enum.
enum class Lone : std::uint8_t {
    Only = 0,
};

template <Lone L>
struct lone_row {
    using type = fe::Row<fe::Effect::Alloc>;
};

static_assert(fe::every_enumerator_lifted<Lone, lone_row>());

}  // namespace

int main() {
    // The rows are types; the one runtime fact is that a lifted row
    // constructs and reports its size.
    [[maybe_unused]] fe::lift_row_t<reaches_disk> disk_row{};
    [[maybe_unused]] fe::lift_row_t<reaches_nothing> empty_row{};
    if (decltype(disk_row)::size != 2) return 1;
    if (decltype(empty_row)::size != 0) return 2;
    return 0;
}
