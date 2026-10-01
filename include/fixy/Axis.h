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
// binding that says nothing about an axis sits at that pole.  A
// relaxation is explicit.  There is no engagement tier: fn<T> is legal
// and strictest on every axis.
//
// The strict pole is the weakest claim.  A grade is a claim of one of two
// kinds, and axis_traits<A>::claim says which:
//
//   - A Right is a right that the binding claims for its body: to cause
//     an effect, to be copied or dropped, to release a secret, to hold a
//     protocol, to mutate, to self-call, to wrap, to read a stale value.
//     The weakest claim of a right is no right.
//   - A Fact is a fact that the binding states about its value or its
//     body: a predicate, a lifetime, a source, a verification, a layout, a
//     cost, a precision, a space, a depth, a version, a latency tier.
//     The weakest claim of a fact is no fact.
//
// A gate that must have a fact refuses a binding that does not state it.  An
// atom that describes what the body does, such as a system call or a
// park, states a fact.  The right for the action is in the Effect row,
// and the lift of the atom puts it there.  So the pole of a Fact axis
// claims nothing, and the pole of a Right axis grants no right.
// every_pole_is_the_weakest_claim() refuses a Fact axis whose pole claims
// a fact.  It also refuses a Right axis whose pole claims nothing,
// because on a Right axis that pole puts no bound on the body.
//
// The enumerators are append-only.  A trait cites an axis by value, so
// an enumerator inserted in the middle renumbers every axis after it.
// The parenthesised number on each arm is the source dimension in the
// FX catalog this vocabulary is derived from.  Two of those dimensions
// are deliberately absent, marked where they would have fallen.
//
// The letter on each arm is the tier kind of the axis in the FX scheme:
// S semiring, L lattice, T typestate, F foundational, V versioned.  In
// this table the shape of an axis takes the place of its tier kind.
// Many Tier S axes are chains, and each chain is a Lattice here.  Only
// the two counted resources, Usage and Staleness, keep the Semiring
// shape.

#include <foundation/algebra/lattices/ConfLattice.h>
#include <foundation/algebra/lattices/QttSemiring.h>
#include <foundation/effects/Row.h>
#include <foundation/reflect/Enumerate.h>
#include <fixy/Tags.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <string>
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
// exists.  The enum names the wrappers without declaring them, so the
// table has no dependency on any of them.
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
    // self-call. A waiting strategy says nothing about self-call.
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
    // The effect row does not carry control-flow escape. Control flow, call
    // shape, stack use, global state and standard-io each have an axis of
    // their own.
    ControlFlow = 24,  // S  (Crucible extension)
    CallShape = 25,  // S  (Crucible extension)
    StackUse = 26,  // S  (Crucible extension)
    GlobalState = 27,  // S  (Crucible extension)
    Stdio = 28,  // S  (Crucible extension)
    HwInstruction = 29,  // S  (Crucible extension)
    // BarrierStrength grades the memory ordering that a region provides. The
    // standard memory orders are its middle grades, with a compiler barrier
    // below them and a standalone fence above them. The Synchronization axis
    // holds only the six wait strategies.
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

namespace detail {

// Whether one text contains another, for a constant expression.
//
// std::string_view::find null-checks the pointer char_traits::find hands
// back, and GCC 16.2's constant evaluator refuses that comparison on a
// pointer into the template-parameter object std::define_static_string
// returns; indexing the same pointer is accepted, so this walks indices.
// Every header in this layer that searches a generated diagnostic —
// Collision.h, Corpus.h, Reject.h, Insights.h — reads it, which is why
// it sits here rather than in one of them.
[[nodiscard]] consteval bool text_contains(std::string_view haystack, std::string_view needle) noexcept {
    if (needle.size() > haystack.size()) return false;
    for (std::size_t start = 0; start + needle.size() <= haystack.size(); ++start) {
        bool same = true;
        for (std::size_t offset = 0; offset < needle.size(); ++offset) {
            if (haystack[start + offset] != needle[offset]) {
                same = false;
                break;
            }
        }
        if (same) return true;
    }
    return false;
}

}  // namespace detail

// The strict poles that are not a point of a foundation lattice.  Each
// is the claim a binding makes when it says nothing.  The atoms of
// fixy/Atom.h are what a binding names to say more.
namespace pole {

namespace pred {
// The predicate that is true for each value: the Refinement pole, which
// states no fact.
struct True {
    template <typename T>
    [[nodiscard]] static constexpr bool check(const T&) noexcept {
        return true;
    }
};
}  // namespace pred

namespace proto {
struct None {};  // no protocol obligation, so no right to hold a live protocol
}  // namespace proto

enum class ReprKind : std::uint8_t {
    Opaque = 0,  // layout opaque
    C = 1,  // standard layout
    Packed = 2,  // no padding
    Aligned = 3,  // alignment hint
    Simd = 4,  // SIMD-vector layout
    Atomic = 5,  // atomic representation, CAS-capable carrier
};

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

namespace stale {
struct Fresh {};  // no staleness admitted, so no right to read a stale value
}  // namespace stale

// The pole of a Fact axis whose vocabulary has no point of its own that
// states nothing: the binding states no fact on that axis.  A gate that
// must have the fact refuses it.
//
// The axis is the template argument, so one template gives each of
// those axes a pole type of its own and two axes still cannot share
// one.
template <Axis A>
struct Unconstrained {};

}  // namespace pole

// What a grade claims, and so what the weakest claim is.  The header
// comment states the rule, and every_pole_is_the_weakest_claim() reads it.
enum class Claim : std::uint8_t {
    Right = 0,  // a right that the binding claims for its body. The weakest claim is no right
    Fact = 1,  // a fact that the binding states about its value or its body. The weakest claim is no fact
};

// The function tells if a pole claims nothing.  pole::Unconstrained<A>
// claims nothing on its axis by construction.  The other three are the
// points of their vocabularies that state nothing: a predicate that is
// true for each value, a trust that nothing verified, and a layout that
// says nothing.
//
// The function is not a template, so no other file can add a pole to the
// set.  A trait or a variable template is open, because an explicit
// specialization in any translation unit changes the answer there.
[[nodiscard]] consteval bool pole_claims_nothing(std::meta::info candidate) noexcept {
    const std::meta::info named = std::meta::dealias(candidate);
    if (std::meta::has_template_arguments(named) && std::meta::template_of(named) == ^^pole::Unconstrained) {
        return true;
    }
    return named == ^^pole::pred::True || named == ^^tags::trust::Unverified
        || named == ^^std::integral_constant<pole::ReprKind, pole::ReprKind::Opaque>;
}

// The rule, as a check on one pole.  The pole of a Fact axis claims
// nothing.  The pole of a Right axis grants no right, so it is never a
// pole that claims nothing: on a Right axis that pole puts no bound
// on the body.
template <Claim C, class Pole>
concept PoleFitsClaim = (C == Claim::Fact) == pole_claims_nothing(^^Pole);

// Thirteen axes say the same thing: a Fact lattice discharged at
// the type level, no wrapper on the binding, and the pole that claims
// nothing.  The primary below is that sentence, and this roster is the
// opt-in to it.
//
// The roster is what keeps a defined primary from failing open.  A
// defined primary alone would hand a new axis a claim nobody gave it.
// The roster keeps the rejection, and an axis on it costs one line
// instead of the eight lines of a specialisation.
inline constexpr Axis defaulted_axes[] = {
    Axis::Size,        Axis::Synchronization, Axis::FpMode,          Axis::SyscallSurface,
    Axis::ControlFlow, Axis::CallShape,       Axis::StackUse,        Axis::GlobalState,
    Axis::Stdio,       Axis::HwInstruction,   Axis::BarrierStrength, Axis::SimdIsa,
    Axis::MemoryScope,
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
// takes; or `caller_supplied`, for the one axis with no pole.  Every
// axis with a pole also states its `claim`.
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
    static constexpr Claim claim = Claim::Fact;
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
    static constexpr Claim claim = Claim::Fact;
    using strict = pole::pred::True;
};

template <>
struct axis_traits<Axis::Usage> {
    static constexpr Shape shape = Shape::Semiring;
    static constexpr Discharge discharge = Discharge::TypeLevel;
    static constexpr Wrapper wrapper = Wrapper::Qtt;
    static constexpr Claim claim = Claim::Right;
    // The linear grade: consumed exactly once, so no right to copy or drop.
    using strict = std::integral_constant<::foundation::algebra::lattices::QttGrade,
                                          ::foundation::algebra::lattices::QttGrade::One>;
};

template <>
struct axis_traits<Axis::Effect> {
    static constexpr Shape shape = Shape::Row;
    static constexpr Discharge discharge = Discharge::TypeLevel;
    static constexpr Wrapper wrapper = Wrapper::Computation;
    static constexpr Claim claim = Claim::Right;
    using strict = ::foundation::effects::Row<>;
};

template <>
struct axis_traits<Axis::Security> {
    static constexpr Shape shape = Shape::Lattice;
    static constexpr Discharge discharge = Discharge::TypeLevel;
    static constexpr Wrapper wrapper = Wrapper::Secret;
    static constexpr Claim claim = Claim::Right;
    // Classified: observation requires a named declassification.
    // ConfLattice has two points, and this is its top.
    using strict =
        std::integral_constant<::foundation::algebra::lattices::Conf, ::foundation::algebra::lattices::Conf::Secret>;
};

template <>
struct axis_traits<Axis::Protocol> {
    static constexpr Shape shape = Shape::Transition;
    static constexpr Discharge discharge = Discharge::TypeLevel;
    static constexpr Wrapper wrapper = Wrapper::None;
    static constexpr Claim claim = Claim::Right;
    using strict = pole::proto::None;
};

// A lifetime, a source, a precision, a space and a cost are facts about
// the value.  A pole that states one gives each binding that says nothing
// a fact that nothing proved: a value that lives for the whole program,
// an internal source, a bit-exact result, no heap use.  So each pole
// states nothing, and a gate that must have the fact refuses it.
template <>
struct axis_traits<Axis::Lifetime> {
    static constexpr Shape shape = Shape::Lattice;
    static constexpr Discharge discharge = Discharge::TypeLevel;
    static constexpr Wrapper wrapper = Wrapper::OwnedRegion;
    static constexpr Claim claim = Claim::Fact;
    using strict = pole::Unconstrained<Axis::Lifetime>;
};

template <>
struct axis_traits<Axis::Provenance> {
    static constexpr Shape shape = Shape::Lattice;
    static constexpr Discharge discharge = Discharge::TypeLevel;
    static constexpr Wrapper wrapper = Wrapper::Tagged;
    static constexpr Claim claim = Claim::Fact;
    using strict = pole::Unconstrained<Axis::Provenance>;
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
    static constexpr Claim claim = Claim::Fact;
    using strict = tags::trust::Unverified;
};

template <>
struct axis_traits<Axis::Representation> {
    static constexpr Shape shape = Shape::Lattice;
    static constexpr Discharge discharge = Discharge::Reflection;
    static constexpr Wrapper wrapper = Wrapper::None;
    static constexpr Claim claim = Claim::Fact;
    using strict = std::integral_constant<pole::ReprKind, pole::ReprKind::Opaque>;
};

// Observability carries no independent payload.  Its pole is Effect's,
// so a binding that accepts the strict pole for both resolves to the
// same type.  The `strict` alias lets a consumer read the resolved
// pole without going through `derived_from`, and the claim is Effect's.
template <>
struct axis_traits<Axis::Observability> {
    static constexpr Shape shape = Shape::Row;
    static constexpr Discharge discharge = Discharge::TypeLevel;
    static constexpr Wrapper wrapper = Wrapper::None;
    using derived_from = axis_traits<Axis::Effect>;
    static constexpr Claim claim = derived_from::claim;
    using strict = derived_from::strict;
};

template <>
struct axis_traits<Axis::Complexity> {
    static constexpr Shape shape = Shape::Lattice;
    static constexpr Discharge discharge = Discharge::Measurement;
    static constexpr Wrapper wrapper = Wrapper::None;
    static constexpr Claim claim = Claim::Fact;
    using strict = pole::Unconstrained<Axis::Complexity>;
};

template <>
struct axis_traits<Axis::Precision> {
    static constexpr Shape shape = Shape::Lattice;
    static constexpr Discharge discharge = Discharge::Measurement;
    static constexpr Wrapper wrapper = Wrapper::None;
    static constexpr Claim claim = Claim::Fact;
    using strict = pole::Unconstrained<Axis::Precision>;
};

template <>
struct axis_traits<Axis::Space> {
    static constexpr Shape shape = Shape::Lattice;
    static constexpr Discharge discharge = Discharge::Contract;
    static constexpr Wrapper wrapper = Wrapper::None;
    static constexpr Claim claim = Claim::Fact;
    using strict = pole::Unconstrained<Axis::Space>;
};

template <>
struct axis_traits<Axis::Overflow> {
    static constexpr Shape shape = Shape::Structural;
    static constexpr Discharge discharge = Discharge::Contract;
    static constexpr Wrapper wrapper = Wrapper::None;
    static constexpr Claim claim = Claim::Right;
    using strict = std::integral_constant<pole::OverflowMode, pole::OverflowMode::Trap>;
};

template <>
struct axis_traits<Axis::Mutation> {
    static constexpr Shape shape = Shape::Lattice;
    static constexpr Discharge discharge = Discharge::Contract;
    static constexpr Wrapper wrapper = Wrapper::Monotonic;
    static constexpr Claim claim = Claim::Right;
    using strict = std::integral_constant<pole::MutationMode, pole::MutationMode::Immutable>;
};

template <>
struct axis_traits<Axis::Reentrancy> {
    static constexpr Shape shape = Shape::Structural;
    static constexpr Discharge discharge = Discharge::TypeLevel;
    static constexpr Wrapper wrapper = Wrapper::None;
    static constexpr Claim claim = Claim::Right;
    using strict = std::integral_constant<pole::ReentrancyMode, pole::ReentrancyMode::NonReentrant>;
};

// A version is a fact about the interface of the binding.  A pole that
// names version 1 gives each binding that says nothing that version, so
// the pole states no version.
template <>
struct axis_traits<Axis::Version> {
    static constexpr Shape shape = Shape::Structural;
    static constexpr Discharge discharge = Discharge::Contract;
    static constexpr Wrapper wrapper = Wrapper::None;
    static constexpr Claim claim = Claim::Fact;
    using strict = pole::Unconstrained<Axis::Version>;
};

template <>
struct axis_traits<Axis::Staleness> {
    static constexpr Shape shape = Shape::Semiring;
    static constexpr Discharge discharge = Discharge::Contract;
    static constexpr Wrapper wrapper = Wrapper::Stale;
    static constexpr Claim claim = Claim::Right;
    using strict = pole::stale::Fresh;
};

// Regime shares the pole of the thirteen on the roster but not their discharge:
// where in the latency budget a function runs is settled by the bench,
// not by the type.  That one difference is why it keeps a
// specialisation and stays off the roster.
template <>
struct axis_traits<Axis::Regime> {
    static constexpr Shape shape = Shape::Lattice;
    static constexpr Discharge discharge = Discharge::Measurement;
    static constexpr Wrapper wrapper = Wrapper::None;
    static constexpr Claim claim = Claim::Fact;
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
// entry that has since grown a specialisation is stale.  An undefined
// primary would catch only the first, and only by being incomplete at a
// sizeof.
//
// Only the first of the two is witnessed from outside, by the check
// file of this header and by neg_axis_unclassified_axis.  Witnessing the
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

// The rule of the header comment, over the table.  Each axis with a pole
// states its claim, and its pole must be the weakest claim of that kind.
// The walk answers with the names of the axes that break the rule, so
// the diagnostic names each one.
template <Axis A>
concept StatesItsClaim = requires {
    { axis_traits<A>::claim } -> std::convertible_to<Claim>;
};

namespace detail {

[[nodiscard]] consteval std::string axes_whose_pole_is_not_the_weakest_claim_() {
    static constexpr auto axes = std::define_static_array(std::meta::enumerators_of(^^Axis));
    std::string offenders;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : axes) {
        constexpr Axis axis = [:en:];
        if constexpr (!IsCallerSupplied<axis>) {
            bool is_weakest = false;
            if constexpr (StatesItsClaim<axis>) {
                is_weakest = PoleFitsClaim<axis_traits<axis>::claim, typename axis_traits<axis>::strict>;
            }
            if (!is_weakest) {
                if (!offenders.empty()) offenders += ", ";
                offenders += axis_name(axis);
            }
        }
    }
#pragma GCC diagnostic pop
    return offenders;
}

[[nodiscard]] consteval std::string_view weakest_claim_diagnostic_() {
    std::string message =
        "fixy/Axis.h: the strict pole of each axis must be its weakest claim, and these axes break the rule: ";
    message += axes_whose_pole_is_not_the_weakest_claim_();
    message += ".  An axis states its claim in axis_traits<A>::claim.  The pole of a Fact axis must claim nothing: "
               "pole::Unconstrained<A>, or the point of its vocabulary that states nothing.  The pole of a Right axis "
               "must grant no right, so it is never a pole that claims nothing.";
    return std::define_static_string(message);
}

}  // namespace detail

[[nodiscard]] consteval bool every_pole_is_the_weakest_claim() {
    return detail::axes_whose_pole_is_not_the_weakest_claim_().empty();
}

}  // namespace fixy
