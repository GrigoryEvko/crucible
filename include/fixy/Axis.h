#pragma once

// The axis table of fixy.  Every axis is a graded modal type over one
// of four algebra shapes: a Lattice, where information flows up; a
// Semiring, where a resource is used n times; a Row, where an effect
// happens only where a context admits it; a Transition, where
// operations happen in a permitted order.  The axes that carry none of
// the four are Structural: a predicate over the type itself, checked
// by reflection.  A grade is minted through one door, weakened only in
// the safe direction, and discharged by exactly one of four
// mechanisms: at the type level, by a construction contract, by
// reflection, or by measurement.
//
// The table rejects by default.  Each axis has a strict pole, and a
// binding that says nothing about an axis sits at that pole; a
// relaxation is explicit.  There is no engagement tier: fn<T> is legal
// and strictest on every axis.
//
// The enumerators are append-only.  A trait cites an axis by value, so
// an enumerator inserted in the middle renumbers every axis after it.
// The parenthesised number on each arm is the source dimension in the
// FX catalog this vocabulary is derived from.  Two of those dimensions
// are deliberately absent, marked where they would have fallen.
//
// Old spellings: include/crucible/safety/DimensionTraits.h
// (DimensionAxis, tier_of_axis), include/crucible/fixy/Default.h
// (strict_default_for) and include/crucible/safety/Fn.h (the pole
// sentinel types, here under fixy::pole).  The old tier kinds S, L,
// T, F and V become the shape; the old Tier S was a catch-all whose
// chains are lattices here, and only the two counted resources keep
// the semiring.

#include <foundation/algebra/lattices/ConfLattice.h>
#include <foundation/algebra/lattices/QttSemiring.h>
#include <foundation/effects/Row.h>
#include <foundation/reflect/Enumerate.h>
#include <fixy/Tags.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>

namespace fixy {

enum class Shape : std::uint8_t {
    Lattice = 0,  // information flows up: join is the weakening
    Semiring = 1,  // a resource is used n times: add is choice, mul is sequence
    Row = 2,  // an effect happens only where a context admits it
    Transition = 3,  // operations happen in a permitted order
    Structural = 4,  // a predicate over the type, checked by reflection
};

enum class Discharge : std::uint8_t {
    TypeLevel = 0,  // the type carries the proof; nothing runs
    Contract = 1,  // a construction contract checks the claim
    Reflection = 2,  // a reflection walk over the type checks the claim
    Measurement = 3,  // the bench or the cross-vendor CI checks the claim
};

// The wrapper that carries an axis's discipline on a value, where one
// exists.  The wrappers arrive in tasks A10.x; this names them without
// declaring them, so the table has no dependency on any of them.
enum class Wrapper : std::uint8_t {
    None = 0,  // the axis lives on the binding only
    Refined = 1,  // fixy/Refined.h
    Qtt = 2,  // fixy/Qtt.h
    Tagged = 3,  // fixy/Tagged.h
    Secret = 4,  // fixy/Secret.h
    Monotonic = 5,  // fixy/Mutation.h
    Stale = 6,  // fixy/Stale.h
    OwnedRegion = 7,  // fixy/OwnedRegion.h
    Computation = 8,  // foundation/effects/Computation.h
};

enum class Axis : std::uint8_t {
    Type = 0,  // F  (FX dim 1)
    Refinement = 1,  // F  (FX dim 2)
    Usage = 2,  // S  (FX dim 3)
    Effect = 3,  // S  (FX dim 4)
    Security = 4,  // S  (FX dim 5)
    Protocol = 5,  // T  (FX dim 6)
    Lifetime = 6,  // S  (FX dim 7)
    Provenance = 7,  // S  (FX dim 8)
    Trust = 8,  // S  (FX dim 9)
    Representation = 9,  // L  (FX dim 10)
    Observability = 10,  // S  (FX dim 11)
    // FX dim 12, clock domain, is absent. Crucible synthesizes no hardware.
    Complexity = 11,  // S  (FX dim 13)
    Precision = 12,  // S  (FX dim 14)
    Space = 13,  // S  (FX dim 15)
    Overflow = 14,  // S  (FX dim 16)
    // FX dim 17, floating-point order, is absent. Pinning the numerical recipe
    // at the emit layer already fixes the order.
    Mutation = 15,  // S  (FX dim 18)
    Reentrancy = 16,  // S  (FX dim 19)
    Size = 17,  // S  (FX dim 20)
    Version = 18,  // V  (FX dim 21)
    Staleness = 19,  // S  (FX dim 22)
    // Synchronization is not Reentrancy. Reentrancy tracks call-graph
    // self-call. A waiting strategy and a memory order say nothing about
    // self-call.
    Synchronization = 20,  // S  (Crucible extension)
    // Regime is not Complexity. Complexity tracks asymptotic and
    // termination-class bounds. Regime tracks where in the latency budget a
    // function runs. Neither subsumes the other: a cold function can still
    // terminate, and a hot one can still diverge.
    Regime = 21,  // S  (Crucible extension)
    // FpMode is not Precision. Precision tracks the element type. One element
    // type produces bit-different results under different rounding,
    // flush-to-zero and contraction modes, so the mode needs its own axis.
    FpMode = 22,  // S  (Crucible extension)
    // The effect row collapses files, network, mapping and process control
    // into one bit. This axis names the kernel-surface family instead, which
    // admission gates have to tell apart.
    SyscallSurface = 23,  // S  (Crucible extension)
    // Control-flow escape used to fold into the effect row. Call shape, stack
    // use, global state and standard-io had no axis at all.
    ControlFlow = 24,  // S  (Crucible extension)
    CallShape = 25,  // S  (Crucible extension)
    StackUse = 26,  // S  (Crucible extension)
    GlobalState = 27,  // S  (Crucible extension)
    Stdio = 28,  // S  (Crucible extension)
    HwInstruction = 29,  // S  (Crucible extension)
    // BarrierStrength is the standalone hardware-fence ladder, distinct from
    // the memory-order tag that lives on the Synchronization axis.
    BarrierStrength = 30,  // S  (Crucible extension)
    // SimdIsa and MemoryScope are Tier L rather than Tier S because each is a
    // non-distributive partial order: two vendor trunks that meet only at the
    // shared bottom and top.
    SimdIsa = 31,  // L  (Crucible extension)
    // MemoryScope is the visibility scope a publication reaches, where
    // BarrierStrength is the strength of the fence that publishes it. The two
    // compose by nesting the wrappers, never through one lattice operation.
    MemoryScope = 32,  // L  (Crucible extension)
};

inline constexpr std::size_t axis_count = std::meta::enumerators_of(^^Axis).size();

// The name of an axis is its enumerator identifier, read by reflection,
// so a new axis is named the moment it is declared.  A value outside
// the enum yields the sentinel "<unknown Axis>" rather than an empty
// view, so a caller that prints a corrupt byte sees that it was one.
// Constexpr rather than consteval so a runtime diagnostic can call it.
[[nodiscard]] constexpr std::string_view axis_name(Axis axis) noexcept {
    return ::foundation::reflect::enum_name(axis);
}

// The strict poles that are not a point of a foundation lattice.  Each
// is the claim a binding makes when it says nothing, and the relaxed
// spellings beside it are what a binding names to say more.
namespace pole {

namespace pred {
struct True {
    template <typename T>
    [[nodiscard]] static constexpr bool check(const T&) noexcept {
        return true;
    }
};
}  // namespace pred

namespace proto {
struct None {};  // no protocol obligation
}  // namespace proto

namespace lifetime {
struct Static {};  // valid for the entire program
template <auto RegionTag>
struct In {};  // valid within a named region
}  // namespace lifetime

enum class ReprKind : std::uint8_t {
    Opaque = 0,  // layout opaque
    C = 1,  // standard layout
    Packed = 2,  // no padding
    Aligned = 3,  // alignment hint
    Simd = 4,  // SIMD-vector layout
    Atomic = 5,  // atomic representation, CAS-capable carrier
};

namespace cost {
struct Unstated {};  // no claim, and an unbounded cost must be declared
struct Constant {};  // O(1)
template <auto N>
struct Linear {};  // O(N)
template <auto N>
struct Quadratic {};  // O(N^2)
struct Unbounded {};  // explicit unbounded
}  // namespace cost

namespace precision {
struct Exact {};  // bit-exact
struct F32 {};
struct F64 {};
template <auto Bound>
struct Higham {};  // Higham bound
}  // namespace precision

namespace space {
struct Zero {};  // stack only
struct Unbounded {};
template <auto N>
struct Bounded {};
}  // namespace space

enum class OverflowMode : std::uint8_t {
    Trap = 0,  // abort on overflow
    Wrap = 1,  // 2^N modular
    Saturate = 2,  // clamp to T's range
    Widen = 3,  // widen result type
};

enum class MutationMode : std::uint8_t {
    Immutable = 0,  // no in-place mutation
    Mutable = 1,  // arbitrary in-place mutation permitted
    Append = 2,  // append-only mutation
    Monotonic = 3,  // monotonic-advance mutation only
};

enum class ReentrancyMode : std::uint8_t {
    NonReentrant = 0,  // self-call rejected
    Reentrant = 1,  // self-call permitted
    Coroutine = 2,  // suspendable, resumable
};

// Codata observation depth.
namespace size_pol {
struct Unstated {};  // no claim, and a depth must be declared
template <auto Depth>
struct Sized {};
struct Productive {};  // codata
}  // namespace size_pol

namespace stale {
struct Fresh {};  // no staleness admitted
template <auto TauMax>
struct Stale {};
}  // namespace stale

// The axes below are wrapper-only: the claim is per value rather than
// per binding, and the wrapper that carries it goes on the value at
// the call site.  Unconstrained is the strict pole of each: the
// binding makes no claim at all.
//
// The axis is the template argument, so one template gives each of
// those axes a pole type of its own and two axes still cannot share
// one.  Thirteen namespaces holding one empty struct each said the
// same thing thirteen times, and a fourteenth axis had to remember to
// add the fourteenth.
template <Axis A>
struct Unconstrained {};

}  // namespace pole

// Twelve axes are wrapper-only and say exactly the same thing: a
// lattice discharged at the type level, no wrapper on the binding, and
// the unconstrained pole.  The primary below is that sentence, and
// this roster is the opt-in to it.
//
// The roster is what keeps a defined primary from failing open.  An
// undefined primary used to reject a new axis by being incomplete at
// the walk's sizeof; a defined one would hand that axis a claim nobody
// gave it.  Naming the twelve here restores the rejection and costs
// one line per axis instead of seven.
inline constexpr Axis defaulted_axes[] = {
    Axis::Synchronization, Axis::FpMode,          Axis::SyscallSurface, Axis::ControlFlow,
    Axis::CallShape,       Axis::StackUse,        Axis::GlobalState,    Axis::Stdio,
    Axis::HwInstruction,   Axis::BarrierStrength, Axis::SimdIsa,        Axis::MemoryScope,
};

[[nodiscard]] consteval bool axis_is_on_default_roster(Axis axis) noexcept {
    for (const Axis listed : defaulted_axes) {
        if (listed == axis) return true;
    }
    return false;
}

template <Axis A>
inline constexpr bool axis_takes_defaults = axis_is_on_default_roster(A);

// One specialisation per axis that says something the primary does
// not.  A specialisation exposes exactly one of three pole markers:
// `strict`, the strict pole as a type (a value pole is an
// integral_constant); `derived_from`, the axis whose pole this one
// takes; or `caller_supplied`, for the one axis with no pole.
//
// `defaulted` is how the walk tells the primary apart from a
// specialisation.  A specialisation does not declare that member, so
// its absence reads as "an author classified this axis by hand".
template <Axis A>
struct axis_traits {
    static constexpr bool defaulted = true;
    static constexpr Shape shape = Shape::Lattice;
    static constexpr Discharge discharge = Discharge::TypeLevel;
    static constexpr Wrapper wrapper = Wrapper::None;
    using strict = pole::Unconstrained<A>;
};

template <>
struct axis_traits<Axis::Type> {
    static constexpr Shape shape = Shape::Structural;
    static constexpr Discharge discharge = Discharge::Reflection;
    static constexpr Wrapper wrapper = Wrapper::None;
    // There is no default function type.  Every binding names its own.
    static constexpr bool caller_supplied = true;
};

template <>
struct axis_traits<Axis::Refinement> {
    static constexpr Shape shape = Shape::Structural;
    static constexpr Discharge discharge = Discharge::Contract;
    static constexpr Wrapper wrapper = Wrapper::Refined;
    using strict = pole::pred::True;
};

template <>
struct axis_traits<Axis::Usage> {
    static constexpr Shape shape = Shape::Semiring;
    static constexpr Discharge discharge = Discharge::TypeLevel;
    static constexpr Wrapper wrapper = Wrapper::Qtt;
    // The linear grade: consumed exactly once.
    using strict = std::integral_constant<::foundation::algebra::lattices::QttGrade,
                                          ::foundation::algebra::lattices::QttGrade::One>;
};

template <>
struct axis_traits<Axis::Effect> {
    static constexpr Shape shape = Shape::Row;
    static constexpr Discharge discharge = Discharge::TypeLevel;
    static constexpr Wrapper wrapper = Wrapper::Computation;
    using strict = ::foundation::effects::Row<>;
};

template <>
struct axis_traits<Axis::Security> {
    static constexpr Shape shape = Shape::Lattice;
    static constexpr Discharge discharge = Discharge::TypeLevel;
    static constexpr Wrapper wrapper = Wrapper::Secret;
    // Classified: observation requires a named declassification.  The
    // old five-level SecLevel collapsed into the two points of
    // ConfLattice when Secret was extracted, and this is its top.
    using strict =
        std::integral_constant<::foundation::algebra::lattices::Conf, ::foundation::algebra::lattices::Conf::Secret>;
};

template <>
struct axis_traits<Axis::Protocol> {
    static constexpr Shape shape = Shape::Transition;
    static constexpr Discharge discharge = Discharge::TypeLevel;
    static constexpr Wrapper wrapper = Wrapper::None;
    using strict = pole::proto::None;
};

template <>
struct axis_traits<Axis::Lifetime> {
    static constexpr Shape shape = Shape::Lattice;
    static constexpr Discharge discharge = Discharge::TypeLevel;
    static constexpr Wrapper wrapper = Wrapper::OwnedRegion;
    using strict = pole::lifetime::Static;
};

template <>
struct axis_traits<Axis::Provenance> {
    static constexpr Shape shape = Shape::Lattice;
    static constexpr Discharge discharge = Discharge::TypeLevel;
    static constexpr Wrapper wrapper = Wrapper::Tagged;
    using strict = tags::source::FromInternal;
};

// Unverified is the bottom of the integrity lattice.  Defaulting to
// Verified would assert maximum integrity for every unannotated
// binding, which fails open.  Verified is earned: a caller that has
// discharged the proof obligation engages it explicitly at the binding
// site.
template <>
struct axis_traits<Axis::Trust> {
    static constexpr Shape shape = Shape::Lattice;
    static constexpr Discharge discharge = Discharge::TypeLevel;
    static constexpr Wrapper wrapper = Wrapper::Tagged;
    using strict = tags::trust::Unverified;
};

template <>
struct axis_traits<Axis::Representation> {
    static constexpr Shape shape = Shape::Lattice;
    static constexpr Discharge discharge = Discharge::Reflection;
    static constexpr Wrapper wrapper = Wrapper::None;
    using strict = std::integral_constant<pole::ReprKind, pole::ReprKind::Opaque>;
};

// Observability carries no independent payload.  Its pole is Effect's,
// so a binding that accepts the strict pole for both resolves to the
// same type.  The `strict` alias lets a consumer read the resolved
// pole without going through `derived_from`.
template <>
struct axis_traits<Axis::Observability> {
    static constexpr Shape shape = Shape::Row;
    static constexpr Discharge discharge = Discharge::TypeLevel;
    static constexpr Wrapper wrapper = Wrapper::None;
    using derived_from = axis_traits<Axis::Effect>;
    using strict = derived_from::strict;
};

template <>
struct axis_traits<Axis::Complexity> {
    static constexpr Shape shape = Shape::Lattice;
    static constexpr Discharge discharge = Discharge::Measurement;
    static constexpr Wrapper wrapper = Wrapper::None;
    using strict = pole::cost::Unstated;
};

template <>
struct axis_traits<Axis::Precision> {
    static constexpr Shape shape = Shape::Lattice;
    static constexpr Discharge discharge = Discharge::Measurement;
    static constexpr Wrapper wrapper = Wrapper::None;
    using strict = pole::precision::Exact;
};

template <>
struct axis_traits<Axis::Space> {
    static constexpr Shape shape = Shape::Lattice;
    static constexpr Discharge discharge = Discharge::Contract;
    static constexpr Wrapper wrapper = Wrapper::None;
    using strict = pole::space::Zero;
};

template <>
struct axis_traits<Axis::Overflow> {
    static constexpr Shape shape = Shape::Structural;
    static constexpr Discharge discharge = Discharge::Contract;
    static constexpr Wrapper wrapper = Wrapper::None;
    using strict = std::integral_constant<pole::OverflowMode, pole::OverflowMode::Trap>;
};

template <>
struct axis_traits<Axis::Mutation> {
    static constexpr Shape shape = Shape::Lattice;
    static constexpr Discharge discharge = Discharge::Contract;
    static constexpr Wrapper wrapper = Wrapper::Monotonic;
    using strict = std::integral_constant<pole::MutationMode, pole::MutationMode::Immutable>;
};

template <>
struct axis_traits<Axis::Reentrancy> {
    static constexpr Shape shape = Shape::Structural;
    static constexpr Discharge discharge = Discharge::TypeLevel;
    static constexpr Wrapper wrapper = Wrapper::None;
    using strict = std::integral_constant<pole::ReentrancyMode, pole::ReentrancyMode::NonReentrant>;
};

template <>
struct axis_traits<Axis::Size> {
    static constexpr Shape shape = Shape::Lattice;
    static constexpr Discharge discharge = Discharge::TypeLevel;
    static constexpr Wrapper wrapper = Wrapper::None;
    using strict = pole::size_pol::Unstated;
};

template <>
struct axis_traits<Axis::Version> {
    static constexpr Shape shape = Shape::Structural;
    static constexpr Discharge discharge = Discharge::Contract;
    static constexpr Wrapper wrapper = Wrapper::None;
    using strict = std::integral_constant<std::uint32_t, 1u>;
};

template <>
struct axis_traits<Axis::Staleness> {
    static constexpr Shape shape = Shape::Semiring;
    static constexpr Discharge discharge = Discharge::Contract;
    static constexpr Wrapper wrapper = Wrapper::Stale;
    using strict = pole::stale::Fresh;
};

// Regime shares the pole of the twelve above but not their discharge:
// where in the latency budget a function runs is settled by the bench,
// not by the type.  That one difference is why it keeps a
// specialisation and stays off the roster.
template <>
struct axis_traits<Axis::Regime> {
    static constexpr Shape shape = Shape::Lattice;
    static constexpr Discharge discharge = Discharge::Measurement;
    static constexpr Wrapper wrapper = Wrapper::None;
    using strict = pole::Unconstrained<Axis::Regime>;
};

template <Axis A>
concept HasStrictPole = requires { typename axis_traits<A>::strict; };

template <Axis A>
concept HasDerivedPole = requires { typename axis_traits<A>::derived_from; };

template <Axis A>
concept IsCallerSupplied = requires {
    { axis_traits<A>::caller_supplied } -> std::convertible_to<bool>;
} && axis_traits<A>::caller_supplied;

// True for an axis that reached the primary, false for one that a
// specialisation classified.  Only the primary declares `defaulted`.
template <Axis A>
concept TakesDefaultTraits = requires {
    { axis_traits<A>::defaulted } -> std::convertible_to<bool>;
};

// The roster and the specialisations partition the enum, and this one
// comparison catches both ways of breaking that.  A new axis nobody
// classified lands on the primary while off the roster.  A roster
// entry that has since grown a specialisation is stale.  The undefined
// primary this table used to carry caught only the first, and only by
// being incomplete at a sizeof.
//
// Only the first of the two is witnessed from outside, by the
// assertion below and by neg_axis_unclassified_axis.  Witnessing the
// second would mean specialising axis_traits for an axis the walk has
// already instantiated, which is separately ill-formed, so no fixture
// can reach it.
template <Axis A>
concept AxisIsClassified = (TakesDefaultTraits<A> == axis_takes_defaults<A>);

// Walks the enum.  Each axis must sit on the default roster or carry a
// specialisation, must classify as exactly one of caller-supplied,
// strict or derived, and must name a shape and a discharge that the
// switches below know.
[[nodiscard]] consteval bool every_axis_has_traits() noexcept {
    static constexpr auto axes = std::define_static_array(std::meta::enumerators_of(^^Axis));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : axes) {
        constexpr Axis axis = [:en:];
        if (!AxisIsClassified<axis>) return false;
        constexpr bool has_caller = IsCallerSupplied<axis>;
        constexpr bool has_strict = HasStrictPole<axis>;
        constexpr bool has_derived = HasDerivedPole<axis>;
        // A derived axis also exposes `strict`, pre-resolved through
        // `derived_from`.  That is one classification, not two, so
        // strict counts as an independent marker only when no derived
        // marker is present.
        constexpr int strict_independent = (has_strict && !has_derived) ? 1 : 0;
        constexpr int marker_count = (has_caller ? 1 : 0) + strict_independent + (has_derived ? 1 : 0);
        if (marker_count != 1) return false;
        if (std::meta::enumerators_of(^^Shape).size() <= static_cast<std::size_t>(axis_traits<axis>::shape)) {
            return false;
        }
        if (std::meta::enumerators_of(^^Discharge).size() <= static_cast<std::size_t>(axis_traits<axis>::discharge)) {
            return false;
        }
        if (std::meta::enumerators_of(^^Wrapper).size() <= static_cast<std::size_t>(axis_traits<axis>::wrapper)) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}

[[nodiscard]] consteval std::size_t count_axes_of_shape(Shape shape) noexcept {
    static constexpr auto axes = std::define_static_array(std::meta::enumerators_of(^^Axis));
    std::size_t count = 0;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : axes) {
        constexpr Axis axis = [:en:];
        if (axis_traits<axis>::shape == shape) ++count;
    }
#pragma GCC diagnostic pop
    return count;
}

static_assert(every_axis_has_traits(),
              "fixy::Axis: an axis is neither on defaulted_axes nor carries an axis_traits "
              "specialisation, or it carries both, or the one it carries does not classify as "
              "exactly one of caller-supplied, strict or derived.  A wrapper-only axis whose claim "
              "is a lattice discharged at the type level joins defaulted_axes and needs nothing "
              "else.  Any other axis needs a specialisation next to the others above.");

// The walk above rejects an axis only if the comparison it rests on can
// answer no.  A value one past the enum stands in for the next
// enumerator somebody adds: it reaches the primary, like that
// enumerator would, and it is not on the roster, like that enumerator
// would not be.  Without this line the partition check could pass by
// being vacuous, which is how the undefined primary it replaces would
// have been quietly weakened.
static_assert(!AxisIsClassified<static_cast<Axis>(axis_count)>,
              "fixy::Axis: the partition check must reject an axis the table has not classified.  "
              "It answers yes for a value one past the enum, so it would answer yes for a new "
              "enumerator too, and defaulted_axes would stop being an opt-in.");

static_assert(std::is_same_v<axis_traits<Axis::Observability>::strict, axis_traits<Axis::Effect>::strict>,
              "Observability's derived pole must round-trip to Effect's strict pole.");

}  // namespace fixy
