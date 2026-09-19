// Sentinel TU for fixy/Axis.h and fixy/Tags.h: the table covers every
// axis, the axis count is what reflection reads off the enum, the
// poles the header comment promises are the ones the table names, and
// every shape has at least one axis.

#include <fixy/Axis.h>
#include <fixy/Tags.h>

#include <foundation/algebra/lattices/QttSemiring.h>
#include <foundation/effects/Row.h>

#include <cstddef>
#include <iterator>
#include <meta>
#include <string_view>
#include <type_traits>

namespace {

using ::fixy::Axis;
using ::fixy::axis_traits;
using ::fixy::Discharge;
using ::fixy::Shape;
using ::fixy::Wrapper;

static_assert(::fixy::every_axis_has_traits());

// The name of every axis is the identifier reflection reads off its
// enumerator, and a value outside the enum yields the sentinel.
[[nodiscard]] consteval bool every_axis_name_is_its_identifier() noexcept {
    static constexpr auto axes = std::define_static_array(std::meta::enumerators_of(^^::fixy::Axis));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : axes) {
        if (::fixy::axis_name([:en:]) != std::meta::identifier_of(en)) return false;
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_axis_name_is_its_identifier());
static_assert(::fixy::axis_name(static_cast<Axis>(::fixy::axis_count)) == "<unknown Axis>");

// The pin: the count reflection reads off the enum is the thirty-three
// the design names.
static_assert(std::meta::enumerators_of(^^::fixy::Axis).size() == 33);
static_assert(::fixy::axis_count == std::meta::enumerators_of(^^::fixy::Axis).size());

// The ordinals are frozen: the first, the two after the absent FX
// dimensions, and the last.
static_assert(static_cast<std::size_t>(Axis::Type) == 0);
static_assert(static_cast<std::size_t>(Axis::Complexity) == 11);
static_assert(static_cast<std::size_t>(Axis::Mutation) == 15);
static_assert(static_cast<std::size_t>(Axis::MemoryScope) == 32);
static_assert(static_cast<std::size_t>(Axis::MemoryScope) + 1 == ::fixy::axis_count);

// The poles the header comment promises.
static_assert(std::is_same_v<axis_traits<Axis::Trust>::strict, ::fixy::tags::trust::Unverified>);
static_assert(std::is_same_v<axis_traits<Axis::Provenance>::strict, ::fixy::tags::source::FromInternal>);
static_assert(std::is_same_v<axis_traits<Axis::Effect>::strict, ::foundation::effects::Row<>>);
static_assert(axis_traits<Axis::Usage>::strict::value == ::foundation::algebra::lattices::QttGrade::One);
static_assert(axis_traits<Axis::Version>::strict::value == 1u);
static_assert(std::is_same_v<axis_traits<Axis::Refinement>::strict, ::fixy::pole::pred::True>);
static_assert(std::is_same_v<axis_traits<Axis::Complexity>::strict, ::fixy::pole::cost::Unstated>);
static_assert(std::is_same_v<axis_traits<Axis::MemoryScope>::strict, ::fixy::pole::Unconstrained<Axis::MemoryScope>>);
static_assert(axis_traits<Axis::Type>::caller_supplied);
static_assert(::fixy::IsCallerSupplied<Axis::Type>);
static_assert(!::fixy::HasStrictPole<Axis::Type>);
static_assert(::fixy::HasDerivedPole<Axis::Observability>);
static_assert(!::fixy::HasDerivedPole<Axis::Effect>);

// The unconstrained pole is one template, so two axes that both take
// it still hold different types. Collapsing them would let a binding
// that constrains one axis satisfy another.
static_assert(
    !std::is_same_v<::fixy::pole::Unconstrained<Axis::MemoryScope>, ::fixy::pole::Unconstrained<Axis::SimdIsa>>);
static_assert(!std::is_same_v<axis_traits<Axis::Stdio>::strict, axis_traits<Axis::GlobalState>::strict>);
static_assert(std::is_empty_v<::fixy::pole::Unconstrained<Axis::Stdio>>);

// The roster and the specialisations partition the enum. Twelve axes
// take the primary's defaults; the other twenty-one say something the
// primary does not.
static_assert(std::size(::fixy::defaulted_axes) == 12);
static_assert(::fixy::axis_count - std::size(::fixy::defaulted_axes) == 21);
static_assert(::fixy::TakesDefaultTraits<Axis::MemoryScope>);
static_assert(::fixy::axis_takes_defaults<Axis::MemoryScope>);
static_assert(!::fixy::TakesDefaultTraits<Axis::Trust>);
static_assert(!::fixy::axis_takes_defaults<Axis::Trust>);

// Regime shares the pole of the twelve but not their discharge, so it
// stays off the roster.
static_assert(!::fixy::axis_takes_defaults<Axis::Regime>);
static_assert(!::fixy::TakesDefaultTraits<Axis::Regime>);
static_assert(axis_traits<Axis::Regime>::discharge == Discharge::Measurement);
static_assert(std::is_same_v<axis_traits<Axis::Regime>::strict, ::fixy::pole::Unconstrained<Axis::Regime>>);

// Every axis on the roster reads back the primary's sentence.
[[nodiscard]] consteval bool every_defaulted_axis_reads_the_primary() noexcept {
    static constexpr auto axes = std::define_static_array(std::meta::enumerators_of(^^::fixy::Axis));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : axes) {
        constexpr Axis axis = [:en:];
        if constexpr (::fixy::axis_takes_defaults<axis>) {
            if (axis_traits<axis>::shape != Shape::Lattice) return false;
            if (axis_traits<axis>::discharge != Discharge::TypeLevel) return false;
            if (axis_traits<axis>::wrapper != Wrapper::None) return false;
            if (!std::is_same_v<typename axis_traits<axis>::strict, ::fixy::pole::Unconstrained<axis>>) return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_defaulted_axis_reads_the_primary());

// The shapes the algebra forces: the two counted resources, the row,
// the protocol order, and the two predicates over the type.
static_assert(axis_traits<Axis::Effect>::shape == Shape::Row);
static_assert(axis_traits<Axis::Usage>::shape == Shape::Semiring);
static_assert(axis_traits<Axis::Staleness>::shape == Shape::Semiring);
static_assert(axis_traits<Axis::Protocol>::shape == Shape::Transition);
static_assert(axis_traits<Axis::Type>::shape == Shape::Structural);
static_assert(axis_traits<Axis::Trust>::shape == Shape::Lattice);

// Each shape has at least one axis, and the shapes partition the axes.
[[nodiscard]] consteval bool every_shape_is_used() noexcept {
    static constexpr auto shapes = std::define_static_array(std::meta::enumerators_of(^^::fixy::Shape));
    std::size_t total = 0;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : shapes) {
        const std::size_t count = ::fixy::count_axes_of_shape([:en:]);
        if (count == 0) return false;
        total += count;
    }
#pragma GCC diagnostic pop
    return total == ::fixy::axis_count;
}
static_assert(every_shape_is_used());

// The wrappers named by the table are the ones the design assigns.
static_assert(axis_traits<Axis::Refinement>::wrapper == Wrapper::Refined);
static_assert(axis_traits<Axis::Usage>::wrapper == Wrapper::Qtt);
static_assert(axis_traits<Axis::Trust>::wrapper == Wrapper::Tagged);
static_assert(axis_traits<Axis::Security>::wrapper == Wrapper::Secret);
static_assert(axis_traits<Axis::Mutation>::wrapper == Wrapper::Monotonic);
static_assert(axis_traits<Axis::Staleness>::wrapper == Wrapper::Stale);
static_assert(axis_traits<Axis::Lifetime>::wrapper == Wrapper::OwnedRegion);
static_assert(axis_traits<Axis::Effect>::wrapper == Wrapper::Computation);
static_assert(axis_traits<Axis::Version>::wrapper == Wrapper::None);

// Each discharge mechanism has at least one axis.
static_assert(axis_traits<Axis::Usage>::discharge == Discharge::TypeLevel);
static_assert(axis_traits<Axis::Refinement>::discharge == Discharge::Contract);
static_assert(axis_traits<Axis::Type>::discharge == Discharge::Reflection);
static_assert(axis_traits<Axis::Precision>::discharge == Discharge::Measurement);

static_assert(::fixy::axis_name(Axis::Type) == "Type");
static_assert(::fixy::axis_name(Axis::MemoryScope) == "MemoryScope");

// The tags are empty types, and the policy tags derive from the marker
// base that closes their set.
static_assert(std::is_empty_v<::fixy::tags::source::Sanitized>);
static_assert(std::is_empty_v<::fixy::tags::vessel_trust::FromPytorch>);
static_assert(std::is_empty_v<::fixy::tags::hash_family::FamilyB>);
static_assert(std::is_empty_v<::fixy::tags::secret_policy::AuditedLogging>);
static_assert(
    std::is_base_of_v<::fixy::tags::secret_policy::secret_policy_base, ::fixy::tags::secret_policy::AuthorizedReplay>);
static_assert(std::is_final_v<::fixy::tags::secret_policy::AuthorizedReplay>);
static_assert(::fixy::tags::version::V<3>::number == 3);
static_assert(::fixy::tags::source::X86Pinned::arch == ::fixy::tags::source::ArchTag::X86);

}  // namespace

int main() {
    // The name walk is constexpr but not consteval, so one runtime
    // call keeps it covered by the sanitizers, and one runtime call
    // with a value outside the enum covers the sentinel arm.
    volatile auto axis = Axis::Regime;
    const std::string_view name = ::fixy::axis_name(axis);
    if (name != "Regime") return 1;
    volatile auto outside = static_cast<Axis>(::fixy::axis_count);
    if (::fixy::axis_name(outside) != "<unknown Axis>") return 2;
    return 0;
}
