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
#include <fixy/Tags.h>

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

[[nodiscard]] constexpr std::string_view axis_name(Axis axis) noexcept {
    switch (axis) {
        case Axis::Type:
            return "Type";
        case Axis::Refinement:
            return "Refinement";
        case Axis::Usage:
            return "Usage";
        case Axis::Effect:
            return "Effect";
        case Axis::Security:
            return "Security";
        case Axis::Protocol:
            return "Protocol";
        case Axis::Lifetime:
            return "Lifetime";
        case Axis::Provenance:
            return "Provenance";
        case Axis::Trust:
            return "Trust";
        case Axis::Representation:
            return "Representation";
        case Axis::Observability:
            return "Observability";
        case Axis::Complexity:
            return "Complexity";
        case Axis::Precision:
            return "Precision";
        case Axis::Space:
            return "Space";
        case Axis::Overflow:
            return "Overflow";
        case Axis::Mutation:
            return "Mutation";
        case Axis::Reentrancy:
            return "Reentrancy";
        case Axis::Size:
            return "Size";
        case Axis::Version:
            return "Version";
        case Axis::Staleness:
            return "Staleness";
        case Axis::Synchronization:
            return "Synchronization";
        case Axis::Regime:
            return "Regime";
        case Axis::FpMode:
            return "FpMode";
        case Axis::SyscallSurface:
            return "SyscallSurface";
        case Axis::ControlFlow:
            return "ControlFlow";
        case Axis::CallShape:
            return "CallShape";
        case Axis::StackUse:
            return "StackUse";
        case Axis::GlobalState:
            return "GlobalState";
        case Axis::Stdio:
            return "Stdio";
        case Axis::HwInstruction:
            return "HwInstruction";
        case Axis::BarrierStrength:
            return "BarrierStrength";
        case Axis::SimdIsa:
            return "SimdIsa";
        case Axis::MemoryScope:
            return "MemoryScope";
        default:
            return std::string_view{"<unknown Axis>"};
    }
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
// the call site.  Each namespace exists so the table can name a pole
// for its axis.  Unconstrained is the strict pole in each: the binding
// makes no claim at all.

namespace sync {
struct Unconstrained {};
}  // namespace sync

namespace regime {
struct Unconstrained {};
}  // namespace regime

namespace fp_mode {
struct Unconstrained {};
}  // namespace fp_mode

namespace syscall {
struct Unconstrained {};
}  // namespace syscall

namespace control_flow {
struct Unconstrained {};
}  // namespace control_flow
namespace call_shape {
struct Unconstrained {};
}  // namespace call_shape
namespace stack_use {
struct Unconstrained {};
}  // namespace stack_use
namespace global_state {
struct Unconstrained {};
}  // namespace global_state
namespace stdio {
struct Unconstrained {};
}  // namespace stdio

namespace hw_instruction {
struct Unconstrained {};
}  // namespace hw_instruction
namespace barrier_strength {
struct Unconstrained {};
}  // namespace barrier_strength
namespace simd_isa {
struct Unconstrained {};
}  // namespace simd_isa

namespace memory_scope {
struct Unconstrained {};
}  // namespace memory_scope

}  // namespace pole

// One specialisation per axis.  A specialisation exposes exactly one
// of three pole markers: `strict`, the strict pole as a type (a value
// pole is an integral_constant); `derived_from`, the axis whose pole
// this one takes; or `caller_supplied`, for the one axis with no pole.
template <Axis A>
struct axis_traits;

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

template <>
struct axis_traits<Axis::Synchronization> {
    static constexpr Shape shape = Shape::Lattice;
    static constexpr Discharge discharge = Discharge::TypeLevel;
    static constexpr Wrapper wrapper = Wrapper::None;
    using strict = pole::sync::Unconstrained;
};

template <>
struct axis_traits<Axis::Regime> {
    static constexpr Shape shape = Shape::Lattice;
    static constexpr Discharge discharge = Discharge::Measurement;
    static constexpr Wrapper wrapper = Wrapper::None;
    using strict = pole::regime::Unconstrained;
};

template <>
struct axis_traits<Axis::FpMode> {
    static constexpr Shape shape = Shape::Lattice;
    static constexpr Discharge discharge = Discharge::TypeLevel;
    static constexpr Wrapper wrapper = Wrapper::None;
    using strict = pole::fp_mode::Unconstrained;
};

template <>
struct axis_traits<Axis::SyscallSurface> {
    static constexpr Shape shape = Shape::Lattice;
    static constexpr Discharge discharge = Discharge::TypeLevel;
    static constexpr Wrapper wrapper = Wrapper::None;
    using strict = pole::syscall::Unconstrained;
};

template <>
struct axis_traits<Axis::ControlFlow> {
    static constexpr Shape shape = Shape::Lattice;
    static constexpr Discharge discharge = Discharge::TypeLevel;
    static constexpr Wrapper wrapper = Wrapper::None;
    using strict = pole::control_flow::Unconstrained;
};

template <>
struct axis_traits<Axis::CallShape> {
    static constexpr Shape shape = Shape::Lattice;
    static constexpr Discharge discharge = Discharge::TypeLevel;
    static constexpr Wrapper wrapper = Wrapper::None;
    using strict = pole::call_shape::Unconstrained;
};

template <>
struct axis_traits<Axis::StackUse> {
    static constexpr Shape shape = Shape::Lattice;
    static constexpr Discharge discharge = Discharge::TypeLevel;
    static constexpr Wrapper wrapper = Wrapper::None;
    using strict = pole::stack_use::Unconstrained;
};

template <>
struct axis_traits<Axis::GlobalState> {
    static constexpr Shape shape = Shape::Lattice;
    static constexpr Discharge discharge = Discharge::TypeLevel;
    static constexpr Wrapper wrapper = Wrapper::None;
    using strict = pole::global_state::Unconstrained;
};

template <>
struct axis_traits<Axis::Stdio> {
    static constexpr Shape shape = Shape::Lattice;
    static constexpr Discharge discharge = Discharge::TypeLevel;
    static constexpr Wrapper wrapper = Wrapper::None;
    using strict = pole::stdio::Unconstrained;
};

template <>
struct axis_traits<Axis::HwInstruction> {
    static constexpr Shape shape = Shape::Lattice;
    static constexpr Discharge discharge = Discharge::TypeLevel;
    static constexpr Wrapper wrapper = Wrapper::None;
    using strict = pole::hw_instruction::Unconstrained;
};

template <>
struct axis_traits<Axis::BarrierStrength> {
    static constexpr Shape shape = Shape::Lattice;
    static constexpr Discharge discharge = Discharge::TypeLevel;
    static constexpr Wrapper wrapper = Wrapper::None;
    using strict = pole::barrier_strength::Unconstrained;
};

template <>
struct axis_traits<Axis::SimdIsa> {
    static constexpr Shape shape = Shape::Lattice;
    static constexpr Discharge discharge = Discharge::TypeLevel;
    static constexpr Wrapper wrapper = Wrapper::None;
    using strict = pole::simd_isa::Unconstrained;
};

template <>
struct axis_traits<Axis::MemoryScope> {
    static constexpr Shape shape = Shape::Lattice;
    static constexpr Discharge discharge = Discharge::TypeLevel;
    static constexpr Wrapper wrapper = Wrapper::None;
    using strict = pole::memory_scope::Unconstrained;
};

template <Axis A>
concept HasStrictPole = requires { typename axis_traits<A>::strict; };

template <Axis A>
concept HasDerivedPole = requires { typename axis_traits<A>::derived_from; };

template <Axis A>
concept IsCallerSupplied = requires {
    { axis_traits<A>::caller_supplied } -> std::convertible_to<bool>;
} && axis_traits<A>::caller_supplied;

// Walks the enum.  A missing specialisation is an incomplete-type
// error inside the walk, at the sizeof; a present one must classify
// as exactly one of caller-supplied, strict or derived, and must name
// a shape and a discharge that the switches below know.
[[nodiscard]] consteval bool every_axis_has_traits() noexcept {
    static constexpr auto axes = std::define_static_array(std::meta::enumerators_of(^^Axis));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : axes) {
        constexpr Axis axis = [:en:];
        static_assert(sizeof(axis_traits<axis>) > 0);
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

[[nodiscard]] consteval bool every_axis_has_name() noexcept {
    static constexpr auto axes = std::define_static_array(std::meta::enumerators_of(^^Axis));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : axes) {
        const auto name = axis_name([:en:]);
        if (name == std::string_view{"<unknown Axis>"}) return false;
        if (name.empty()) return false;
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

static_assert(every_axis_has_traits(), "fixy::Axis: an axis has no axis_traits specialisation, or one that "
                                       "does not classify as exactly one of caller-supplied, strict or "
                                       "derived.  Add the specialisation next to the others above.");

static_assert(every_axis_has_name(), "fixy::axis_name is missing an arm for at least one Axis; add the arm "
                                     "or the new axis leaks the '<unknown Axis>' sentinel.");

static_assert(std::is_same_v<axis_traits<Axis::Observability>::strict, axis_traits<Axis::Effect>::strict>,
              "Observability's derived pole must round-trip to Effect's strict pole.");

}  // namespace fixy
