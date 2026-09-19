#pragma once

// A resolver needs one metafunction from DimensionAxis to the value or type
// the substrate's Fn<> carries on that axis.  Without the projection the
// resolver would have to switch on DimensionAxis at every per-axis slot.
//
// No default is defined here.  Every specialization aliases the substrate's
// own default, so the two cannot drift.

#include <crucible/fixy/Dim.h>
#include <crucible/safety/Fn.h>
#include <crucible/safety/_Tagged.h>
#include <crucible/effects/_EffectRow.h>

#include <concepts>
#include <cstdint>
#include <meta>
#include <type_traits>

namespace crucible::fixy {

template <dim::DimensionAxis D>
struct strict_default_for;

// A specialization exposes either `type` or the pair `(value_type, value)`,
// never both.  Which one depends on whether the axis is type-valued or
// enum-valued in the substrate.

template <>
struct strict_default_for<dim::DimensionAxis::Type> {
    // There is no default function type.  Every binding names its own.
    static constexpr bool caller_supplied = true;
};

template <>
struct strict_default_for<dim::DimensionAxis::Refinement> {
    using type = safety::fn::pred::True;
};

template <>
struct strict_default_for<dim::DimensionAxis::Usage> {
    using value_type = safety::fn::UsageMode;
    static constexpr value_type value = safety::fn::UsageMode::Linear;
};

template <>
struct strict_default_for<dim::DimensionAxis::Effect> {
    using type = effects::Row<>;
};

template <>
struct strict_default_for<dim::DimensionAxis::Security> {
    using value_type = safety::fn::SecLevel;
    static constexpr value_type value = safety::fn::SecLevel::Classified;
};

template <>
struct strict_default_for<dim::DimensionAxis::Protocol> {
    using type = safety::fn::proto::None;
};

template <>
struct strict_default_for<dim::DimensionAxis::Lifetime> {
    using type = safety::fn::lifetime::Static;
};

template <>
struct strict_default_for<dim::DimensionAxis::Provenance> {
    using type = safety::source::FromInternal;
};

// Unverified is the bottom of the integrity lattice.  Defaulting to Verified
// would assert maximum integrity for every unannotated binding, which fails
// open.  Verified is earned: a caller that has discharged the proof
// obligation engages it explicitly at the binding site.
template <>
struct strict_default_for<dim::DimensionAxis::Trust> {
    using type = safety::trust::Unverified;
};

template <>
struct strict_default_for<dim::DimensionAxis::Representation> {
    using value_type = safety::fn::ReprKind;
    static constexpr value_type value = safety::fn::ReprKind::Opaque;
};

template <>
struct strict_default_for<dim::DimensionAxis::Observability> {
    // Observability carries no independent payload.  Its default is
    // Effect's, so a binding that accepts the strict default for both
    // resolves to the same type.  The engagement marker is still required
    // on this axis: it is the author's witness that the axis was
    // considered.  The `type` alias lets a consumer read the resolved value
    // without going through `derived_from`.
    using derived_from = strict_default_for<dim::DimensionAxis::Effect>;
    using type = typename derived_from::type;
};

static_assert(std::is_same_v<typename strict_default_for<dim::DimensionAxis::Observability>::type,
                             typename strict_default_for<dim::DimensionAxis::Effect>::type>,
              "Observability's derived type must round-trip to Effect's strict default.");

static_assert(
    requires { typename strict_default_for<dim::DimensionAxis::Observability>::derived_from; },
    "Observability's `derived_from` alias must exist.  Its presence pins the "
    "half-engaged duality: engagement is required on the axis while the "
    "payload is derived.  Removing the alias means updating the engagement "
    "walk in lockstep.");

template <>
struct strict_default_for<dim::DimensionAxis::Complexity> {
    using type = safety::fn::cost::Unstated;
};

template <>
struct strict_default_for<dim::DimensionAxis::Precision> {
    using type = safety::fn::precision::Exact;
};

template <>
struct strict_default_for<dim::DimensionAxis::Space> {
    using type = safety::fn::space::Zero;
};

template <>
struct strict_default_for<dim::DimensionAxis::Overflow> {
    using value_type = safety::fn::OverflowMode;
    static constexpr value_type value = safety::fn::OverflowMode::Trap;
};

template <>
struct strict_default_for<dim::DimensionAxis::Mutation> {
    using value_type = safety::fn::MutationMode;
    static constexpr value_type value = safety::fn::MutationMode::Immutable;
};

template <>
struct strict_default_for<dim::DimensionAxis::Reentrancy> {
    using value_type = safety::fn::ReentrancyMode;
    static constexpr value_type value = safety::fn::ReentrancyMode::NonReentrant;
};

template <>
struct strict_default_for<dim::DimensionAxis::Size> {
    using type = safety::fn::size_pol::Unstated;
};

template <>
struct strict_default_for<dim::DimensionAxis::Version> {
    using value_type = std::uint32_t;
    static constexpr value_type value = 1u;
};

template <>
struct strict_default_for<dim::DimensionAxis::Staleness> {
    using type = safety::fn::stale::Fresh;
};

// Every axis from here down is wrapper-only.  The discipline lives on the
// value through a Graded wrapper, not in an Fn<> aggregator slot, so there is
// no Fn<int> member to round-trip against and these axes are deliberately
// absent from type_defaults_match_substrate below.  Each strict default is
// Unconstrained: the binding makes no claim at this scope, and a wrapper on a
// value flowing through carries the discipline.
template <>
struct strict_default_for<dim::DimensionAxis::Synchronization> {
    using type = safety::fn::sync::Unconstrained;
};

template <>
struct strict_default_for<dim::DimensionAxis::Regime> {
    using type = safety::fn::regime::Unconstrained;
};

template <>
struct strict_default_for<dim::DimensionAxis::FpMode> {
    using type = safety::fn::fp_mode::Unconstrained;
};

template <>
struct strict_default_for<dim::DimensionAxis::SyscallSurface> {
    using type = safety::fn::syscall::Unconstrained;
};

template <>
struct strict_default_for<dim::DimensionAxis::ControlFlow> {
    using type = safety::fn::control_flow::Unconstrained;
};

template <>
struct strict_default_for<dim::DimensionAxis::CallShape> {
    using type = safety::fn::call_shape::Unconstrained;
};

template <>
struct strict_default_for<dim::DimensionAxis::StackUse> {
    using type = safety::fn::stack_use::Unconstrained;
};

template <>
struct strict_default_for<dim::DimensionAxis::GlobalState> {
    using type = safety::fn::global_state::Unconstrained;
};

template <>
struct strict_default_for<dim::DimensionAxis::Stdio> {
    using type = safety::fn::stdio::Unconstrained;
};

template <>
struct strict_default_for<dim::DimensionAxis::HwInstruction> {
    using type = safety::fn::hw_instruction::Unconstrained;
};

template <>
struct strict_default_for<dim::DimensionAxis::BarrierStrength> {
    using type = safety::fn::barrier_strength::Unconstrained;
};

template <>
struct strict_default_for<dim::DimensionAxis::SimdIsa> {
    using type = safety::fn::simd_isa::Unconstrained;
};

template <>
struct strict_default_for<dim::DimensionAxis::MemoryScope> {
    using type = safety::fn::memory_scope::Unconstrained;
};

template <dim::DimensionAxis D>
concept HasStrictDefault = requires { typename strict_default_for<D>::type; } || requires {
    typename strict_default_for<D>::value_type;
    { strict_default_for<D>::value };
};

template <dim::DimensionAxis D>
concept HasDerivedDefault = requires { typename strict_default_for<D>::derived_from; };

template <dim::DimensionAxis D>
concept IsCallerSupplied = requires {
    { strict_default_for<D>::caller_supplied } -> std::convertible_to<bool>;
} && strict_default_for<D>::caller_supplied;

namespace detail::default_coverage {

[[nodiscard]] consteval bool every_axis_resolves() noexcept {
    static constexpr auto resolve_axes =
        std::define_static_array(std::meta::enumerators_of(^^::crucible::safety::DimensionAxis));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : resolve_axes) {
        constexpr auto axis_v = [:en:];
        constexpr bool has_caller = IsCallerSupplied<axis_v>;
        constexpr bool has_strict = HasStrictDefault<axis_v>;
        constexpr bool has_derived = HasDerivedDefault<axis_v>;
        // A derived axis also exposes `type`, pre-resolved through
        // `derived_from`.  That is one classification, not two, so strict
        // counts as an independent slot only when no derived marker is
        // present.
        constexpr int strict_indep = (has_strict && !has_derived) ? 1 : 0;
        constexpr int match_count = (has_caller ? 1 : 0) + strict_indep + (has_derived ? 1 : 0);
        if (match_count != 1) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}

[[nodiscard]] consteval bool type_defaults_match_substrate() noexcept {
    using DF = safety::fn::Fn<int>;
    return std::is_same_v<typename strict_default_for<dim::DimensionAxis::Refinement>::type, DF::refinement_t>
        && std::is_same_v<typename strict_default_for<dim::DimensionAxis::Effect>::type, DF::effect_row_t>
        && std::is_same_v<typename strict_default_for<dim::DimensionAxis::Protocol>::type, DF::protocol_t>
        && std::is_same_v<typename strict_default_for<dim::DimensionAxis::Lifetime>::type, DF::lifetime_t>
        && std::is_same_v<typename strict_default_for<dim::DimensionAxis::Provenance>::type, DF::source_t>
        && std::is_same_v<typename strict_default_for<dim::DimensionAxis::Trust>::type, DF::trust_t>
        && std::is_same_v<typename strict_default_for<dim::DimensionAxis::Complexity>::type, DF::cost_t>
        && std::is_same_v<typename strict_default_for<dim::DimensionAxis::Precision>::type, DF::precision_t>
        && std::is_same_v<typename strict_default_for<dim::DimensionAxis::Space>::type, DF::space_t>
        && std::is_same_v<typename strict_default_for<dim::DimensionAxis::Size>::type, DF::size_t_>
        && std::is_same_v<typename strict_default_for<dim::DimensionAxis::Staleness>::type, DF::staleness_t>;
}

[[nodiscard]] consteval bool enum_defaults_match_substrate() noexcept {
    using DF = safety::fn::Fn<int>;
    return strict_default_for<dim::DimensionAxis::Usage>::value == DF::usage_v
        && strict_default_for<dim::DimensionAxis::Security>::value == DF::security_v
        && strict_default_for<dim::DimensionAxis::Representation>::value == DF::repr_v
        && strict_default_for<dim::DimensionAxis::Overflow>::value == DF::overflow_v
        && strict_default_for<dim::DimensionAxis::Mutation>::value == DF::mutation_v
        && strict_default_for<dim::DimensionAxis::Reentrancy>::value == DF::reentrancy_v
        && strict_default_for<dim::DimensionAxis::Version>::value == DF::version_v;
}

}  // namespace detail::default_coverage

static_assert(detail::default_coverage::every_axis_resolves(),
              "fixy::Default — at least one DimensionAxis enumerator does not "
              "have a strict_default_for specialization (or has multiple "
              "conflicting role markers).  Each axis must classify as exactly "
              "ONE of: caller-supplied (Type), has-strict-default (most axes), "
              "or has-derived-default (Observability).  Add the missing "
              "specialization, then re-run.");

static_assert(detail::default_coverage::type_defaults_match_substrate(),
              "fixy::Default — a type-valued strict-default aliased here has "
              "drifted from safety::fn::Fn<int>'s shipped default.  Likely "
              "cause: the substrate's per-axis default was changed without "
              "updating fixy/Default.h alongside.  Re-align fixy/Default.h's "
              "specialization with the substrate's authoritative default in its "
              "class template parameter list.");

static_assert(detail::default_coverage::enum_defaults_match_substrate(),
              "fixy::Default — an enum-valued strict-default aliased here has "
              "drifted from safety::fn::Fn<int>'s shipped default.  Same fix "
              "as the type-defaults assertion above.");

}  // namespace crucible::fixy
