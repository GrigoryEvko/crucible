// Sentinel TU for fixy/Axis.h and fixy/Tags.h: the table covers every
// axis, the axis count is what reflection reads off the enum, the
// poles the header comment promises are the ones the table names, and
// every shape has at least one axis.

#include <fixy/Axis.h>
#include <fixy/Tags.h>

#include <foundation/algebra/lattices/QttSemiring.h>
#include <foundation/effects/Row.h>

#include <cstddef>
#include <meta>
#include <string_view>
#include <type_traits>

namespace {

using ::fixy::Axis;
using ::fixy::axis_record;
using ::fixy::Discharge;
using ::fixy::strict_pole_t;
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
        if (::fixy::axis_name([:en:]) != ::foundation::core::identifier_text(en)) return false;
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
static_assert(std::is_same_v<strict_pole_t<Axis::Trust>, ::fixy::tags::trust::Unverified>);
static_assert(std::is_same_v<strict_pole_t<Axis::Effect>, ::foundation::effects::Row<>>);
static_assert(strict_pole_t<Axis::Usage>::value == ::foundation::algebra::lattices::QttGrade::One);
static_assert(std::is_same_v<strict_pole_t<Axis::Refinement>, ::fixy::pole::pred::True>);
static_assert(std::is_same_v<strict_pole_t<Axis::MemoryScope>, ::fixy::pole::Unconstrained<Axis::MemoryScope>>);

// A Fact axis states no fact at its pole.  A pole that names a fact gives
// that fact to each binding that says nothing: a value that lives for the
// whole program, an internal source, a bit-exact result, no heap use,
// version 1.
static_assert(std::is_same_v<strict_pole_t<Axis::Lifetime>, ::fixy::pole::Unconstrained<Axis::Lifetime>>);
static_assert(std::is_same_v<strict_pole_t<Axis::Provenance>, ::fixy::pole::Unconstrained<Axis::Provenance>>);
static_assert(std::is_same_v<strict_pole_t<Axis::Precision>, ::fixy::pole::Unconstrained<Axis::Precision>>);
static_assert(std::is_same_v<strict_pole_t<Axis::Space>, ::fixy::pole::Unconstrained<Axis::Space>>);
static_assert(std::is_same_v<strict_pole_t<Axis::Complexity>, ::fixy::pole::Unconstrained<Axis::Complexity>>);
static_assert(std::is_same_v<strict_pole_t<Axis::Size>, ::fixy::pole::Unconstrained<Axis::Size>>);
static_assert(std::is_same_v<strict_pole_t<Axis::Version>, ::fixy::pole::Unconstrained<Axis::Version>>);

// Each axis states its claim, and the rule of the header comment holds
// over the whole table.
static_assert(axis_record(Axis::Effect).claim == ::fixy::Claim::Right);
static_assert(axis_record(Axis::Observability).claim == ::fixy::Claim::Right,
              "Observability takes the claim of the axis it derives from");
static_assert(axis_record(Axis::Staleness).claim == ::fixy::Claim::Right);
static_assert(axis_record(Axis::Trust).claim == ::fixy::Claim::Fact);
static_assert(axis_record(Axis::Space).claim == ::fixy::Claim::Fact);
static_assert(axis_record(Axis::MemoryScope).claim == ::fixy::Claim::Fact);
static_assert(::fixy::every_pole_is_the_weakest_claim());
static_assert(axis_record(Axis::Type).pole_source == ::fixy::PoleSource::CallerSupplied);
static_assert(::fixy::IsCallerSupplied<Axis::Type>);
static_assert(!::fixy::HasStrictPole<Axis::Type>);
static_assert(::fixy::HasDerivedPole<Axis::Observability>);
static_assert(!::fixy::HasDerivedPole<Axis::Effect>);

// The unconstrained pole is one template, so two axes that both take
// it still hold different types. Collapsing them would let a binding
// that constrains one axis satisfy another.
static_assert(
    !std::is_same_v<::fixy::pole::Unconstrained<Axis::MemoryScope>, ::fixy::pole::Unconstrained<Axis::SimdIsa>>);
static_assert(!std::is_same_v<strict_pole_t<Axis::Stdio>, strict_pole_t<Axis::GlobalState>>);
static_assert(std::is_empty_v<::fixy::pole::Unconstrained<Axis::Stdio>>);

// Thirteen axes take the default row, and the other twenty have a row of
// their own.
[[nodiscard]] consteval std::size_t count_defaulted_axes() noexcept {
    std::size_t count = 0;
    for (std::size_t position = 0; position < ::fixy::axis_count; ++position) {
        if (axis_record(static_cast<Axis>(position)).is_defaulted) ++count;
    }
    return count;
}
static_assert(count_defaulted_axes() == 13);
static_assert(::fixy::axis_count - count_defaulted_axes() == 20);
static_assert(::fixy::TakesDefaultTraits<Axis::MemoryScope>);
static_assert(!::fixy::TakesDefaultTraits<Axis::Trust>);

// Regime shares the pole of the thirteen but not their discharge, so it
// has a row of its own.
static_assert(!::fixy::TakesDefaultTraits<Axis::Regime>);
static_assert(axis_record(Axis::Regime).discharge == Discharge::Measurement);
static_assert(std::is_same_v<strict_pole_t<Axis::Regime>, ::fixy::pole::Unconstrained<Axis::Regime>>);

// Every axis of the default row reads back the sentence of that row.
[[nodiscard]] consteval bool every_defaulted_axis_reads_the_default_row() noexcept {
    static constexpr auto axes = std::define_static_array(std::meta::enumerators_of(^^::fixy::Axis));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : axes) {
        constexpr Axis axis = [:en:];
        if constexpr (::fixy::TakesDefaultTraits<axis>) {
            constexpr ::fixy::AxisRecord record = axis_record(axis);
            if (record.shape != Shape::Lattice) return false;
            if (record.discharge != Discharge::TypeLevel) return false;
            if (record.wrapper != Wrapper::None) return false;
            if (record.claim != ::fixy::Claim::Fact) return false;
            if (!std::is_same_v<strict_pole_t<axis>, ::fixy::pole::Unconstrained<axis>>) return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_defaulted_axis_reads_the_default_row());

// The shapes the algebra forces: the two counted resources, the row,
// the protocol order, and the two predicates over the type.
static_assert(axis_record(Axis::Effect).shape == Shape::Row);
static_assert(axis_record(Axis::Usage).shape == Shape::Semiring);
static_assert(axis_record(Axis::Staleness).shape == Shape::Semiring);
static_assert(axis_record(Axis::Protocol).shape == Shape::Transition);
static_assert(axis_record(Axis::Type).shape == Shape::Structural);
static_assert(axis_record(Axis::Trust).shape == Shape::Lattice);

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
static_assert(axis_record(Axis::Refinement).wrapper == Wrapper::Refined);
static_assert(axis_record(Axis::Usage).wrapper == Wrapper::Qtt);
static_assert(axis_record(Axis::Trust).wrapper == Wrapper::Tagged);
static_assert(axis_record(Axis::Security).wrapper == Wrapper::Secret);
static_assert(axis_record(Axis::Mutation).wrapper == Wrapper::Monotonic);
static_assert(axis_record(Axis::Staleness).wrapper == Wrapper::Stale);
static_assert(axis_record(Axis::Lifetime).wrapper == Wrapper::OwnedRegion);
static_assert(axis_record(Axis::Effect).wrapper == Wrapper::Computation);
static_assert(axis_record(Axis::Version).wrapper == Wrapper::None);

// Each discharge mechanism has at least one axis.
static_assert(axis_record(Axis::Usage).discharge == Discharge::TypeLevel);
static_assert(axis_record(Axis::Refinement).discharge == Discharge::Contract);
static_assert(axis_record(Axis::Type).discharge == Discharge::Reflection);
static_assert(axis_record(Axis::Precision).discharge == Discharge::Measurement);

static_assert(::fixy::axis_name(Axis::Type) == "Type");
static_assert(::fixy::axis_name(Axis::MemoryScope) == "MemoryScope");

// The tags are empty types, and the policy tags derive from the marker
// base that closes their set.
static_assert(std::is_empty_v<::fixy::tags::source::Sanitized>);
static_assert(std::is_empty_v<::fixy::tags::vessel_trust::FromPytorch>);
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
    ::foundation::core::TextView const name = ::fixy::axis_name(axis);
    if (name != "Regime") return 1;
    volatile auto outside = static_cast<Axis>(::fixy::axis_count);
    if (::fixy::axis_name(outside) != "<unknown Axis>") return 2;
    return 0;
}
