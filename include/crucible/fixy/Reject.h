#pragma once

#include <crucible/fixy/Default.h>
#include <crucible/fixy/Dim.h>
#include <crucible/fixy/_Grant.h>
#include <crucible/fixy/Theory.h>
#include <crucible/safety/_Diagnostic.h>

#include <charconv>
#include <concepts>
#include <cstddef>
#include <meta>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

namespace crucible::fixy {

namespace diag {

#define CRUCIBLE_FIXY_NOT_ENGAGED_TAG(AxisName, AxisDesc)                                     \
    struct FixyNotEngaged_##AxisName final : ::crucible::safety::diag::tag_base {             \
        static constexpr ::std::string_view name = "FixyNotEngaged_" #AxisName;               \
        static constexpr ::std::string_view description =                                     \
            "Dimension '" #AxisName "' (" AxisDesc ") has no engagement marker "              \
            "or relaxation tag in the binding's Grants pack.  Every fixy:: "                  \
            "binding MUST engage with every dimension in the DimensionAxis "                  \
            "universe (cardinality is the reflection-derived "                                \
            "`safety::DIMENSION_AXIS_COUNT`, mirrored by "                                    \
            "`fixy::diag::kFixyCatalogDocstringCardinality` — never hard-code "               \
            "this number in prose) either via an explicit relaxation tag or "                 \
            "via "                                                                            \
            "`grant::accept_default_strict_for<dim::DimensionAxis::" #AxisName ">`.";         \
        static constexpr ::std::string_view remediation =                                     \
            "Add `grant::accept_default_strict_for<dim::DimensionAxis::" #AxisName            \
            ">` to the Grants pack if the binding's behavior on this "                        \
            "axis is the strict default, OR add the appropriate per-axis "                    \
            "relaxation tag (the `crucible::fixy::grant` namespace holds the "                \
            "catalog).";                                                                      \
    };                                                                                        \
    static_assert(::crucible::safety::diag::is_diagnostic_class_v<FixyNotEngaged_##AxisName>, \
                  "FixyNotEngaged_" #AxisName " must inherit safety::diag::tag_base.")

CRUCIBLE_FIXY_NOT_ENGAGED_TAG(Type, "the function type itself");
CRUCIBLE_FIXY_NOT_ENGAGED_TAG(Refinement, "value-level predicate");
CRUCIBLE_FIXY_NOT_ENGAGED_TAG(Usage, "linear / affine / copy / ghost");
CRUCIBLE_FIXY_NOT_ENGAGED_TAG(Effect, "Bg / IO / Alloc / Block / etc.");
CRUCIBLE_FIXY_NOT_ENGAGED_TAG(Security, "Classified / Public / Secret");
CRUCIBLE_FIXY_NOT_ENGAGED_TAG(Protocol, "session-type / state machine");
CRUCIBLE_FIXY_NOT_ENGAGED_TAG(Lifetime, "Static / In<Region>");
CRUCIBLE_FIXY_NOT_ENGAGED_TAG(Provenance, "source provenance tag");
CRUCIBLE_FIXY_NOT_ENGAGED_TAG(Trust, "Verified / Tested / Assumed");
CRUCIBLE_FIXY_NOT_ENGAGED_TAG(Representation, "memory layout discipline");
CRUCIBLE_FIXY_NOT_ENGAGED_TAG(Observability, "derived from Effect — accept only");
CRUCIBLE_FIXY_NOT_ENGAGED_TAG(Complexity, "O(1) / O(N) / O(N²) classification");
CRUCIBLE_FIXY_NOT_ENGAGED_TAG(Precision, "FP error bound");
CRUCIBLE_FIXY_NOT_ENGAGED_TAG(Space, "allocation footprint bound");
CRUCIBLE_FIXY_NOT_ENGAGED_TAG(Overflow, "Trap / Wrap / Saturate / Widen");
CRUCIBLE_FIXY_NOT_ENGAGED_TAG(Mutation, "Immutable / Append / Monotonic / Mutable");
CRUCIBLE_FIXY_NOT_ENGAGED_TAG(Reentrancy, "NonReentrant / Reentrant / Coroutine");
CRUCIBLE_FIXY_NOT_ENGAGED_TAG(Size, "codata observation depth");
CRUCIBLE_FIXY_NOT_ENGAGED_TAG(Version, "schema version number");
CRUCIBLE_FIXY_NOT_ENGAGED_TAG(Staleness, "freshness bound τ");
CRUCIBLE_FIXY_NOT_ENGAGED_TAG(Synchronization,
                              "wait-strategy / memory-order discipline (safety::Wait / safety::MemOrder)");
CRUCIBLE_FIXY_NOT_ENGAGED_TAG(Regime, "operating-regime tier (Hot / Warm / Cold) — safety::HotPath");
CRUCIBLE_FIXY_NOT_ENGAGED_TAG(FpMode, "FP-mode taxonomy (Rounding / Ftz / Contract / TrapMask / Denormal / "
                                      "NanPolicy / InfPolicy / ComplexLayout / LibmPolicy / Reassociate / "
                                      "FpConstant) — Forge phase E.RecipeSelect par/seq composition");
CRUCIBLE_FIXY_NOT_ENGAGED_TAG(SyscallSurface, "syscall-family taxonomy (NoSyscall / VdsoOnly / ReadOnlyState / "
                                              "FileMutation / MemoryMapping / ThreadSync / NetworkIo / "
                                              "ProcessControl / Privilege) — Forge phase E hot-path admission");
CRUCIBLE_FIXY_NOT_ENGAGED_TAG(ControlFlow, "control-flow taxonomy (Pure / AbortOnly / ThrowOnly / MayLongjmp / "
                                           "MaySignal) — permission_fork no-throw + Forge hot-path admission");
CRUCIBLE_FIXY_NOT_ENGAGED_TAG(CallShape, "call-shape taxonomy (Direct / BoundedRecurses<N> / Indirect / "
                                         "Virtual / Unbounded) — bounded-stack + devirtualization gating");
CRUCIBLE_FIXY_NOT_ENGAGED_TAG(StackUse, "stack-frame depth discipline (bounded vs unbounded stack growth)");
CRUCIBLE_FIXY_NOT_ENGAGED_TAG(GlobalState, "global-state surface (none / readonly / thread-local / mutable-global)");
CRUCIBLE_FIXY_NOT_ENGAGED_TAG(Stdio, "C stdio surface (none / reads / writes) — Meyers-singleton + I/O gating");
CRUCIBLE_FIXY_NOT_ENGAGED_TAG(HwInstruction, "hw-instruction capability tier (NoneAllowed / Scalar / Vectorizable / "
                                             "NonDeterministicTsc / PrivilegedMsr) — rdtsc/rdmsr admission gating");
CRUCIBLE_FIXY_NOT_ENGAGED_TAG(BarrierStrength, "memory-fence strength ladder (None / CompilerBarrier / AcquireLoad / "
                                               "ReleaseStore / AcqRel / SeqCst / FullFence) — explicit-fence grant");
CRUCIBLE_FIXY_NOT_ENGAGED_TAG(SimdIsa, "SIMD ISA family (Scalar / Portable / SSE2..AVX512 / NEON..SVE) — "
                                       "Tier-L non-distributive x86×ARM trunk lattice, width pinning");
CRUCIBLE_FIXY_NOT_ENGAGED_TAG(MemoryScope, "memory-visibility scope (Thread / Warp / Cta / Cluster / Gpu accel "
                                           "trunk × Inner(ISH) / Outer(OSH) ARM trunk, joined at Thread / System) "
                                           "— Tier-L non-distributive lattice, scoped-fence + async-copy publish");

#undef CRUCIBLE_FIXY_NOT_ENGAGED_TAG

#define CRUCIBLE_FIXY_DUPLICATE_TAG_EX(AxisName, AxisDesc, ExtraRemediation)                 \
    struct FixyDuplicate_##AxisName final : ::crucible::safety::diag::tag_base {             \
        static constexpr ::std::string_view name = "FixyDuplicate_" #AxisName;               \
        static constexpr ::std::string_view description =                                    \
            "Dimension '" #AxisName "' (" AxisDesc ") is engaged by MORE THAN "              \
            "ONE grant in the binding's Grants pack.  Every fixy:: binding "                 \
            "MUST engage with every dimension EXACTLY ONCE — silent redundant "              \
            "grants hide authorial intent and bypass any tag-vs-tag "                        \
            "disagreement check.";                                                           \
        static constexpr ::std::string_view remediation =                                    \
            "Remove all but one of the grants engaging '" #AxisName "' from the "            \
            "Grants pack." ExtraRemediation;                                                 \
    };                                                                                       \
    static_assert(::crucible::safety::diag::is_diagnostic_class_v<FixyDuplicate_##AxisName>, \
                  "FixyDuplicate_" #AxisName " must inherit safety::diag::tag_base.")

#define CRUCIBLE_FIXY_DUPLICATE_TAG(AxisName, AxisDesc) CRUCIBLE_FIXY_DUPLICATE_TAG_EX(AxisName, AxisDesc, "")

// Type is the only axis the wrapper engages implicitly, so the
// marker-conflict advice is scoped to this one tag.  Folding it into
// the shared macro would hand every other axis remediation advice that
// does not apply to it.
CRUCIBLE_FIXY_DUPLICATE_TAG_EX(Type, "the function type itself",
                               "  Note: explicitly writing `grant::accept_default_strict_for<dim::"
                               "DimensionAxis::Type>` is FORBIDDEN because fixy::fn implicitly "
                               "engages Type — that explicit Type marker would "
                               "trigger a duplicate on the Type axis.  Drop the explicit Type "
                               "marker.");
CRUCIBLE_FIXY_DUPLICATE_TAG(Refinement, "value-level predicate");
CRUCIBLE_FIXY_DUPLICATE_TAG(Usage, "linear / affine / copy / ghost");
CRUCIBLE_FIXY_DUPLICATE_TAG(Effect, "Bg / IO / Alloc / Block / etc.");
CRUCIBLE_FIXY_DUPLICATE_TAG(Security, "Classified / Public / Secret");
CRUCIBLE_FIXY_DUPLICATE_TAG(Protocol, "session-type / state machine");
CRUCIBLE_FIXY_DUPLICATE_TAG(Lifetime, "Static / In<Region>");
CRUCIBLE_FIXY_DUPLICATE_TAG(Provenance, "source provenance tag");
CRUCIBLE_FIXY_DUPLICATE_TAG(Trust, "Verified / Tested / Assumed");
CRUCIBLE_FIXY_DUPLICATE_TAG(Representation, "memory layout discipline");
CRUCIBLE_FIXY_DUPLICATE_TAG(Observability, "derived from Effect — accept only");
CRUCIBLE_FIXY_DUPLICATE_TAG(Complexity, "O(1) / O(N) / O(N²) classification");
CRUCIBLE_FIXY_DUPLICATE_TAG(Precision, "FP error bound");
CRUCIBLE_FIXY_DUPLICATE_TAG(Space, "allocation footprint bound");
CRUCIBLE_FIXY_DUPLICATE_TAG(Overflow, "Trap / Wrap / Saturate / Widen");
CRUCIBLE_FIXY_DUPLICATE_TAG(Mutation, "Immutable / Append / Monotonic / Mutable");
CRUCIBLE_FIXY_DUPLICATE_TAG(Reentrancy, "NonReentrant / Reentrant / Coroutine");
CRUCIBLE_FIXY_DUPLICATE_TAG(Size, "codata observation depth");
CRUCIBLE_FIXY_DUPLICATE_TAG(Version, "schema version number");
CRUCIBLE_FIXY_DUPLICATE_TAG(Staleness, "freshness bound τ");
CRUCIBLE_FIXY_DUPLICATE_TAG(Synchronization,
                            "wait-strategy / memory-order discipline (safety::Wait / safety::MemOrder)");
CRUCIBLE_FIXY_DUPLICATE_TAG(Regime, "operating-regime tier (Hot / Warm / Cold) — safety::HotPath");
CRUCIBLE_FIXY_DUPLICATE_TAG(FpMode, "FP-mode taxonomy (Rounding / Ftz / Contract / TrapMask / Denormal / "
                                    "NanPolicy / InfPolicy / ComplexLayout / LibmPolicy / Reassociate / "
                                    "FpConstant) — Forge phase E.RecipeSelect par/seq composition");
CRUCIBLE_FIXY_DUPLICATE_TAG(SyscallSurface, "syscall-family taxonomy (NoSyscall / VdsoOnly / ReadOnlyState / "
                                            "FileMutation / MemoryMapping / ThreadSync / NetworkIo / "
                                            "ProcessControl / Privilege) — Forge phase E hot-path admission");
CRUCIBLE_FIXY_DUPLICATE_TAG(ControlFlow, "control-flow taxonomy (Pure / AbortOnly / ThrowOnly / MayLongjmp / "
                                         "MaySignal) — permission_fork no-throw + Forge hot-path admission");
CRUCIBLE_FIXY_DUPLICATE_TAG(CallShape, "call-shape taxonomy (Direct / BoundedRecurses<N> / Indirect / "
                                       "Virtual / Unbounded) — bounded-stack + devirtualization gating");
CRUCIBLE_FIXY_DUPLICATE_TAG(StackUse, "stack-frame depth discipline (bounded vs unbounded stack growth)");
CRUCIBLE_FIXY_DUPLICATE_TAG(GlobalState, "global-state surface (none / readonly / thread-local / mutable-global)");
CRUCIBLE_FIXY_DUPLICATE_TAG(Stdio, "C stdio surface (none / reads / writes) — Meyers-singleton + I/O gating");
CRUCIBLE_FIXY_DUPLICATE_TAG(HwInstruction, "hw-instruction capability tier (NoneAllowed / Scalar / Vectorizable / "
                                           "NonDeterministicTsc / PrivilegedMsr) — rdtsc/rdmsr admission gating");
CRUCIBLE_FIXY_DUPLICATE_TAG(BarrierStrength, "memory-fence strength ladder (None / CompilerBarrier / AcquireLoad / "
                                             "ReleaseStore / AcqRel / SeqCst / FullFence) — explicit-fence grant");
CRUCIBLE_FIXY_DUPLICATE_TAG(SimdIsa, "SIMD ISA family (Scalar / Portable / SSE2..AVX512 / NEON..SVE) — "
                                     "Tier-L non-distributive x86×ARM trunk lattice, width pinning");
CRUCIBLE_FIXY_DUPLICATE_TAG(MemoryScope, "memory-visibility scope (Thread / Warp / Cta / Cluster / Gpu accel "
                                         "trunk × Inner(ISH) / Outer(OSH) ARM trunk, joined at Thread / System) "
                                         "— Tier-L non-distributive lattice, scoped-fence + async-copy publish");

#undef CRUCIBLE_FIXY_DUPLICATE_TAG

// One tag, not a per-axis family: a malformed entry is not a grant at
// all, so there is no axis to project it onto.
struct FixyMalformedGrant final : ::crucible::safety::diag::tag_base {
    static constexpr ::std::string_view name = "FixyMalformedGrant";
    static constexpr ::std::string_view description =
        "The Grants pack contains a type that does NOT satisfy "
        "fixy::grant::IsGrantTag (the entry is not final-class, does not "
        "inherit fixy::grant::grant_base, or is a non-grant type entirely "
        "— e.g. a raw `int`, a user-defined struct, or a substrate type). "
        "This defends against trait-spec injection: only grants from the "
        "fixy::grant::* namespace OR `fixy::grant::accept_default_strict_"
        "for<...>` instantiations may engage the discipline axes.";
    static constexpr ::std::string_view remediation = "Audit the Grants pack — every entry must be either (a) a tag "
                                                      "from the fixy::grant::* catalog (e.g. `grant::copy`, `grant::"
                                                      "ghost`, `grant::with<effects::Effect::IO>`, "
                                                      "`grant::declassify<Policy>`), or (b) an explicit acceptance "
                                                      "marker `grant::accept_default_strict_for<dim::DimensionAxis::"
                                                      "<Axis>>` (except the Type axis, which the wrapper engages "
                                                      "implicitly).  Substrate "
                                                      "types reaching this gate are typically the result of a "
                                                      "copy-paste error, a misspelled grant name, or a typo in the "
                                                      "`fixy::fn<Type, ...>` parameter list.";
};
static_assert(::crucible::safety::diag::is_diagnostic_class_v<FixyMalformedGrant>,
              "FixyMalformedGrant must inherit safety::diag::tag_base.");

template <dim::DimensionAxis D>
struct tag_for_axis;

template <>
struct tag_for_axis<dim::DimensionAxis::Type> {
    using type = FixyNotEngaged_Type;
};
template <>
struct tag_for_axis<dim::DimensionAxis::Refinement> {
    using type = FixyNotEngaged_Refinement;
};
template <>
struct tag_for_axis<dim::DimensionAxis::Usage> {
    using type = FixyNotEngaged_Usage;
};
template <>
struct tag_for_axis<dim::DimensionAxis::Effect> {
    using type = FixyNotEngaged_Effect;
};
template <>
struct tag_for_axis<dim::DimensionAxis::Security> {
    using type = FixyNotEngaged_Security;
};
template <>
struct tag_for_axis<dim::DimensionAxis::Protocol> {
    using type = FixyNotEngaged_Protocol;
};
template <>
struct tag_for_axis<dim::DimensionAxis::Lifetime> {
    using type = FixyNotEngaged_Lifetime;
};
template <>
struct tag_for_axis<dim::DimensionAxis::Provenance> {
    using type = FixyNotEngaged_Provenance;
};
template <>
struct tag_for_axis<dim::DimensionAxis::Trust> {
    using type = FixyNotEngaged_Trust;
};
template <>
struct tag_for_axis<dim::DimensionAxis::Representation> {
    using type = FixyNotEngaged_Representation;
};
template <>
struct tag_for_axis<dim::DimensionAxis::Observability> {
    using type = FixyNotEngaged_Observability;
};
template <>
struct tag_for_axis<dim::DimensionAxis::Complexity> {
    using type = FixyNotEngaged_Complexity;
};
template <>
struct tag_for_axis<dim::DimensionAxis::Precision> {
    using type = FixyNotEngaged_Precision;
};
template <>
struct tag_for_axis<dim::DimensionAxis::Space> {
    using type = FixyNotEngaged_Space;
};
template <>
struct tag_for_axis<dim::DimensionAxis::Overflow> {
    using type = FixyNotEngaged_Overflow;
};
template <>
struct tag_for_axis<dim::DimensionAxis::Mutation> {
    using type = FixyNotEngaged_Mutation;
};
template <>
struct tag_for_axis<dim::DimensionAxis::Reentrancy> {
    using type = FixyNotEngaged_Reentrancy;
};
template <>
struct tag_for_axis<dim::DimensionAxis::Size> {
    using type = FixyNotEngaged_Size;
};
template <>
struct tag_for_axis<dim::DimensionAxis::Version> {
    using type = FixyNotEngaged_Version;
};
template <>
struct tag_for_axis<dim::DimensionAxis::Staleness> {
    using type = FixyNotEngaged_Staleness;
};
template <>
struct tag_for_axis<dim::DimensionAxis::Synchronization> {
    using type = FixyNotEngaged_Synchronization;
};
template <>
struct tag_for_axis<dim::DimensionAxis::Regime> {
    using type = FixyNotEngaged_Regime;
};
template <>
struct tag_for_axis<dim::DimensionAxis::FpMode> {
    using type = FixyNotEngaged_FpMode;
};
template <>
struct tag_for_axis<dim::DimensionAxis::SyscallSurface> {
    using type = FixyNotEngaged_SyscallSurface;
};
template <>
struct tag_for_axis<dim::DimensionAxis::ControlFlow> {
    using type = FixyNotEngaged_ControlFlow;
};
template <>
struct tag_for_axis<dim::DimensionAxis::CallShape> {
    using type = FixyNotEngaged_CallShape;
};
template <>
struct tag_for_axis<dim::DimensionAxis::StackUse> {
    using type = FixyNotEngaged_StackUse;
};
template <>
struct tag_for_axis<dim::DimensionAxis::GlobalState> {
    using type = FixyNotEngaged_GlobalState;
};
template <>
struct tag_for_axis<dim::DimensionAxis::Stdio> {
    using type = FixyNotEngaged_Stdio;
};
template <>
struct tag_for_axis<dim::DimensionAxis::HwInstruction> {
    using type = FixyNotEngaged_HwInstruction;
};
template <>
struct tag_for_axis<dim::DimensionAxis::BarrierStrength> {
    using type = FixyNotEngaged_BarrierStrength;
};
template <>
struct tag_for_axis<dim::DimensionAxis::SimdIsa> {
    using type = FixyNotEngaged_SimdIsa;
};
template <>
struct tag_for_axis<dim::DimensionAxis::MemoryScope> {
    using type = FixyNotEngaged_MemoryScope;
};

template <dim::DimensionAxis D>
using tag_for_axis_t = typename tag_for_axis<D>::type;

template <dim::DimensionAxis D>
struct dup_tag_for_axis;

template <>
struct dup_tag_for_axis<dim::DimensionAxis::Type> {
    using type = FixyDuplicate_Type;
};
template <>
struct dup_tag_for_axis<dim::DimensionAxis::Refinement> {
    using type = FixyDuplicate_Refinement;
};
template <>
struct dup_tag_for_axis<dim::DimensionAxis::Usage> {
    using type = FixyDuplicate_Usage;
};
template <>
struct dup_tag_for_axis<dim::DimensionAxis::Effect> {
    using type = FixyDuplicate_Effect;
};
template <>
struct dup_tag_for_axis<dim::DimensionAxis::Security> {
    using type = FixyDuplicate_Security;
};
template <>
struct dup_tag_for_axis<dim::DimensionAxis::Protocol> {
    using type = FixyDuplicate_Protocol;
};
template <>
struct dup_tag_for_axis<dim::DimensionAxis::Lifetime> {
    using type = FixyDuplicate_Lifetime;
};
template <>
struct dup_tag_for_axis<dim::DimensionAxis::Provenance> {
    using type = FixyDuplicate_Provenance;
};
template <>
struct dup_tag_for_axis<dim::DimensionAxis::Trust> {
    using type = FixyDuplicate_Trust;
};
template <>
struct dup_tag_for_axis<dim::DimensionAxis::Representation> {
    using type = FixyDuplicate_Representation;
};
template <>
struct dup_tag_for_axis<dim::DimensionAxis::Observability> {
    using type = FixyDuplicate_Observability;
};
template <>
struct dup_tag_for_axis<dim::DimensionAxis::Complexity> {
    using type = FixyDuplicate_Complexity;
};
template <>
struct dup_tag_for_axis<dim::DimensionAxis::Precision> {
    using type = FixyDuplicate_Precision;
};
template <>
struct dup_tag_for_axis<dim::DimensionAxis::Space> {
    using type = FixyDuplicate_Space;
};
template <>
struct dup_tag_for_axis<dim::DimensionAxis::Overflow> {
    using type = FixyDuplicate_Overflow;
};
template <>
struct dup_tag_for_axis<dim::DimensionAxis::Mutation> {
    using type = FixyDuplicate_Mutation;
};
template <>
struct dup_tag_for_axis<dim::DimensionAxis::Reentrancy> {
    using type = FixyDuplicate_Reentrancy;
};
template <>
struct dup_tag_for_axis<dim::DimensionAxis::Size> {
    using type = FixyDuplicate_Size;
};
template <>
struct dup_tag_for_axis<dim::DimensionAxis::Version> {
    using type = FixyDuplicate_Version;
};
template <>
struct dup_tag_for_axis<dim::DimensionAxis::Staleness> {
    using type = FixyDuplicate_Staleness;
};
template <>
struct dup_tag_for_axis<dim::DimensionAxis::Synchronization> {
    using type = FixyDuplicate_Synchronization;
};
template <>
struct dup_tag_for_axis<dim::DimensionAxis::Regime> {
    using type = FixyDuplicate_Regime;
};
template <>
struct dup_tag_for_axis<dim::DimensionAxis::FpMode> {
    using type = FixyDuplicate_FpMode;
};
template <>
struct dup_tag_for_axis<dim::DimensionAxis::SyscallSurface> {
    using type = FixyDuplicate_SyscallSurface;
};
template <>
struct dup_tag_for_axis<dim::DimensionAxis::ControlFlow> {
    using type = FixyDuplicate_ControlFlow;
};
template <>
struct dup_tag_for_axis<dim::DimensionAxis::CallShape> {
    using type = FixyDuplicate_CallShape;
};
template <>
struct dup_tag_for_axis<dim::DimensionAxis::StackUse> {
    using type = FixyDuplicate_StackUse;
};
template <>
struct dup_tag_for_axis<dim::DimensionAxis::GlobalState> {
    using type = FixyDuplicate_GlobalState;
};
template <>
struct dup_tag_for_axis<dim::DimensionAxis::Stdio> {
    using type = FixyDuplicate_Stdio;
};
template <>
struct dup_tag_for_axis<dim::DimensionAxis::HwInstruction> {
    using type = FixyDuplicate_HwInstruction;
};
template <>
struct dup_tag_for_axis<dim::DimensionAxis::BarrierStrength> {
    using type = FixyDuplicate_BarrierStrength;
};
template <>
struct dup_tag_for_axis<dim::DimensionAxis::SimdIsa> {
    using type = FixyDuplicate_SimdIsa;
};
template <>
struct dup_tag_for_axis<dim::DimensionAxis::MemoryScope> {
    using type = FixyDuplicate_MemoryScope;
};

template <dim::DimensionAxis D>
using dup_tag_for_axis_t = typename dup_tag_for_axis<D>::type;

// Without this sentinel a missing dup_tag_for_axis specialization
// compiles clean until the first use site and surfaces there as a
// "no type named 'type'" template error rather than a cardinality gap.

namespace detail::reject_dup_tag_self_test {

[[nodiscard]] consteval bool every_axis_has_dup_tag() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^::crucible::safety::DimensionAxis));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        constexpr auto axis = ([:en:]);
        constexpr bool has_dup_tag = requires { typename dup_tag_for_axis<axis>::type; };
        if (!has_dup_tag) return false;
        using DupT = typename dup_tag_for_axis<axis>::type;
        if (!::crucible::safety::diag::is_diagnostic_class_v<DupT>) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}

static_assert(every_axis_has_dup_tag(), "a DimensionAxis enumerator has no corresponding "
                                        "dup_tag_for_axis<D>::type specialization (or the "
                                        "specialization names a type that does not inherit "
                                        "safety::diag::tag_base).  Add a CRUCIBLE_FIXY_DUP_TAG(...) "
                                        "invocation for the new axis followed by a template <> struct "
                                        "dup_tag_for_axis<dim::DimensionAxis::NewAxis> { using type = "
                                        "FixyDuplicate_NewAxis; }; specialization.");

}  // namespace detail::reject_dup_tag_self_test

// Fixy tags are enumerated here rather than in the substrate's closed
// diagnostic Catalog, which is reserved for substrate-axis violations.
// The runtime category for a fixy diagnostic is the DimensionAxis
// itself, so no separate category enum is minted: a second enum would
// duplicate the axis universe and give it its own drift surface.
//
// The tuple is append-only in DimensionAxis enumerator order.  The
// bijection self-test below fires if an append lands out of order or
// misses one of the paired lookups.

using FixyCatalog = ::std::tuple<FixyNotEngaged_Type,  //  0
                                 FixyNotEngaged_Refinement,  //  1
                                 FixyNotEngaged_Usage,  //  2
                                 FixyNotEngaged_Effect,  //  3
                                 FixyNotEngaged_Security,  //  4
                                 FixyNotEngaged_Protocol,  //  5
                                 FixyNotEngaged_Lifetime,  //  6
                                 FixyNotEngaged_Provenance,  //  7
                                 FixyNotEngaged_Trust,  //  8
                                 FixyNotEngaged_Representation,  //  9
                                 FixyNotEngaged_Observability,  // 10
                                 FixyNotEngaged_Complexity,  // 11
                                 FixyNotEngaged_Precision,  // 12
                                 FixyNotEngaged_Space,  // 13
                                 FixyNotEngaged_Overflow,  // 14
                                 FixyNotEngaged_Mutation,  // 15
                                 FixyNotEngaged_Reentrancy,  // 16
                                 FixyNotEngaged_Size,  // 17
                                 FixyNotEngaged_Version,  // 18
                                 FixyNotEngaged_Staleness,  // 19
                                 FixyNotEngaged_Synchronization,  // 20
                                 FixyNotEngaged_Regime,  // 21
                                 FixyNotEngaged_FpMode,  // 22
                                 FixyNotEngaged_SyscallSurface,  // 23
                                 FixyNotEngaged_ControlFlow,  // 24
                                 FixyNotEngaged_CallShape,  // 25
                                 FixyNotEngaged_StackUse,  // 26
                                 FixyNotEngaged_GlobalState,  // 27
                                 FixyNotEngaged_Stdio,  // 28
                                 FixyNotEngaged_HwInstruction,  // 29
                                 FixyNotEngaged_BarrierStrength,  // 30
                                 FixyNotEngaged_SimdIsa,  // 31
                                 FixyNotEngaged_MemoryScope  // 32
                                 >;

inline constexpr ::std::size_t fixy_catalog_size = ::std::tuple_size_v<FixyCatalog>;

// Append-only, in the same index order as FixyCatalog.

using FixyDuplicateCatalog = ::std::tuple<FixyDuplicate_Type,  //  0
                                          FixyDuplicate_Refinement,  //  1
                                          FixyDuplicate_Usage,  //  2
                                          FixyDuplicate_Effect,  //  3
                                          FixyDuplicate_Security,  //  4
                                          FixyDuplicate_Protocol,  //  5
                                          FixyDuplicate_Lifetime,  //  6
                                          FixyDuplicate_Provenance,  //  7
                                          FixyDuplicate_Trust,  //  8
                                          FixyDuplicate_Representation,  //  9
                                          FixyDuplicate_Observability,  // 10
                                          FixyDuplicate_Complexity,  // 11
                                          FixyDuplicate_Precision,  // 12
                                          FixyDuplicate_Space,  // 13
                                          FixyDuplicate_Overflow,  // 14
                                          FixyDuplicate_Mutation,  // 15
                                          FixyDuplicate_Reentrancy,  // 16
                                          FixyDuplicate_Size,  // 17
                                          FixyDuplicate_Version,  // 18
                                          FixyDuplicate_Staleness,  // 19
                                          FixyDuplicate_Synchronization,  // 20
                                          FixyDuplicate_Regime,  // 21
                                          FixyDuplicate_FpMode,  // 22
                                          FixyDuplicate_SyscallSurface,  // 23
                                          FixyDuplicate_ControlFlow,  // 24
                                          FixyDuplicate_CallShape,  // 25
                                          FixyDuplicate_StackUse,  // 26
                                          FixyDuplicate_GlobalState,  // 27
                                          FixyDuplicate_Stdio,  // 28
                                          FixyDuplicate_HwInstruction,  // 29
                                          FixyDuplicate_BarrierStrength,  // 30
                                          FixyDuplicate_SimdIsa,  // 31
                                          FixyDuplicate_MemoryScope  // 32
                                          >;

inline constexpr ::std::size_t fixy_duplicate_catalog_size = ::std::tuple_size_v<FixyDuplicateCatalog>;

using FixyMalformedCatalog = ::std::tuple<FixyMalformedGrant>;

inline constexpr ::std::size_t fixy_malformed_catalog_size = ::std::tuple_size_v<FixyMalformedCatalog>;

namespace detail::fixy_catalog {

template <typename T, typename Tuple>
struct in_tuple_impl;

template <typename T, typename... Us>
struct in_tuple_impl<T, ::std::tuple<Us...>> : ::std::bool_constant<(::std::is_same_v<T, Us> || ...)> {};

}  // namespace detail::fixy_catalog

template <typename T>
inline constexpr bool is_fixy_diag_v = detail::fixy_catalog::in_tuple_impl<T, FixyCatalog>::value
                                    || detail::fixy_catalog::in_tuple_impl<T, FixyDuplicateCatalog>::value
                                    || detail::fixy_catalog::in_tuple_impl<T, FixyMalformedCatalog>::value;

template <typename Tag>
struct axis_for_tag;

template <>
struct axis_for_tag<FixyNotEngaged_Type> {
    static constexpr auto value = dim::DimensionAxis::Type;
};
template <>
struct axis_for_tag<FixyNotEngaged_Refinement> {
    static constexpr auto value = dim::DimensionAxis::Refinement;
};
template <>
struct axis_for_tag<FixyNotEngaged_Usage> {
    static constexpr auto value = dim::DimensionAxis::Usage;
};
template <>
struct axis_for_tag<FixyNotEngaged_Effect> {
    static constexpr auto value = dim::DimensionAxis::Effect;
};
template <>
struct axis_for_tag<FixyNotEngaged_Security> {
    static constexpr auto value = dim::DimensionAxis::Security;
};
template <>
struct axis_for_tag<FixyNotEngaged_Protocol> {
    static constexpr auto value = dim::DimensionAxis::Protocol;
};
template <>
struct axis_for_tag<FixyNotEngaged_Lifetime> {
    static constexpr auto value = dim::DimensionAxis::Lifetime;
};
template <>
struct axis_for_tag<FixyNotEngaged_Provenance> {
    static constexpr auto value = dim::DimensionAxis::Provenance;
};
template <>
struct axis_for_tag<FixyNotEngaged_Trust> {
    static constexpr auto value = dim::DimensionAxis::Trust;
};
template <>
struct axis_for_tag<FixyNotEngaged_Representation> {
    static constexpr auto value = dim::DimensionAxis::Representation;
};
template <>
struct axis_for_tag<FixyNotEngaged_Observability> {
    static constexpr auto value = dim::DimensionAxis::Observability;
};
template <>
struct axis_for_tag<FixyNotEngaged_Complexity> {
    static constexpr auto value = dim::DimensionAxis::Complexity;
};
template <>
struct axis_for_tag<FixyNotEngaged_Precision> {
    static constexpr auto value = dim::DimensionAxis::Precision;
};
template <>
struct axis_for_tag<FixyNotEngaged_Space> {
    static constexpr auto value = dim::DimensionAxis::Space;
};
template <>
struct axis_for_tag<FixyNotEngaged_Overflow> {
    static constexpr auto value = dim::DimensionAxis::Overflow;
};
template <>
struct axis_for_tag<FixyNotEngaged_Mutation> {
    static constexpr auto value = dim::DimensionAxis::Mutation;
};
template <>
struct axis_for_tag<FixyNotEngaged_Reentrancy> {
    static constexpr auto value = dim::DimensionAxis::Reentrancy;
};
template <>
struct axis_for_tag<FixyNotEngaged_Size> {
    static constexpr auto value = dim::DimensionAxis::Size;
};
template <>
struct axis_for_tag<FixyNotEngaged_Version> {
    static constexpr auto value = dim::DimensionAxis::Version;
};
template <>
struct axis_for_tag<FixyNotEngaged_Staleness> {
    static constexpr auto value = dim::DimensionAxis::Staleness;
};
template <>
struct axis_for_tag<FixyNotEngaged_Synchronization> {
    static constexpr auto value = dim::DimensionAxis::Synchronization;
};
template <>
struct axis_for_tag<FixyNotEngaged_Regime> {
    static constexpr auto value = dim::DimensionAxis::Regime;
};
template <>
struct axis_for_tag<FixyNotEngaged_FpMode> {
    static constexpr auto value = dim::DimensionAxis::FpMode;
};
template <>
struct axis_for_tag<FixyNotEngaged_SyscallSurface> {
    static constexpr auto value = dim::DimensionAxis::SyscallSurface;
};
template <>
struct axis_for_tag<FixyNotEngaged_ControlFlow> {
    static constexpr auto value = dim::DimensionAxis::ControlFlow;
};
template <>
struct axis_for_tag<FixyNotEngaged_CallShape> {
    static constexpr auto value = dim::DimensionAxis::CallShape;
};
template <>
struct axis_for_tag<FixyNotEngaged_StackUse> {
    static constexpr auto value = dim::DimensionAxis::StackUse;
};
template <>
struct axis_for_tag<FixyNotEngaged_GlobalState> {
    static constexpr auto value = dim::DimensionAxis::GlobalState;
};
template <>
struct axis_for_tag<FixyNotEngaged_Stdio> {
    static constexpr auto value = dim::DimensionAxis::Stdio;
};
template <>
struct axis_for_tag<FixyNotEngaged_HwInstruction> {
    static constexpr auto value = dim::DimensionAxis::HwInstruction;
};
template <>
struct axis_for_tag<FixyNotEngaged_BarrierStrength> {
    static constexpr auto value = dim::DimensionAxis::BarrierStrength;
};
template <>
struct axis_for_tag<FixyNotEngaged_SimdIsa> {
    static constexpr auto value = dim::DimensionAxis::SimdIsa;
};
template <>
struct axis_for_tag<FixyNotEngaged_MemoryScope> {
    static constexpr auto value = dim::DimensionAxis::MemoryScope;
};

template <typename Tag>
inline constexpr dim::DimensionAxis axis_for_tag_v = axis_for_tag<Tag>::value;

namespace detail::fixy_catalog {

inline constexpr ::std::size_t kDimAxisCount = []() consteval {
    return ::std::meta::enumerators_of(^^::crucible::safety::DimensionAxis).size();
}();

static_assert(fixy_catalog_size == kDimAxisCount, "FixyCatalog cardinality drifted from DimensionAxis cardinality. "
                                                  "Adding a new DimensionAxis enumerator requires (a) adding the "
                                                  "matching FixyNotEngaged_<Axis> tag via the macro above, (b) "
                                                  "appending to FixyCatalog in DimensionAxis order, (c) the matching "
                                                  "tag_for_axis specialization, and (d) the matching axis_for_tag "
                                                  "specialization.");

// The diagnostic strings above cite this constant by name instead of
// spelling an axis count.  A literal count in a string would go stale
// the moment a new axis lands and nothing would catch it.
inline constexpr ::std::size_t kFixyCatalogDocstringCardinality = kDimAxisCount;

static_assert(kFixyCatalogDocstringCardinality == kDimAxisCount,
              "kFixyCatalogDocstringCardinality drifted from the "
              "reflection-derived DimensionAxis enumerator count.  If you added "
              "a DimensionAxis enumerator (and the matching FixyNotEngaged_* tag "
              "+ FixyCatalog entry + tag_for_axis / axis_for_tag specs), update "
              "kFixyCatalogDocstringCardinality to match AND search this header's "
              "doc-block for any human-readable count words (for example, "
              "'twenty-four') that need to grow in lockstep.");

template <::std::size_t I>
[[nodiscard]] consteval bool catalog_bijection_at() noexcept {
    using TagAtI = ::std::tuple_element_t<I, FixyCatalog>;
    constexpr auto axis_v = axis_for_tag_v<TagAtI>;
    using TagViaForward = tag_for_axis_t<axis_v>;
    return ::std::is_same_v<TagAtI, TagViaForward> && static_cast<::std::size_t>(axis_v) == I;
}

template <::std::size_t... Is>
[[nodiscard]] consteval bool catalog_bijection_holds(::std::index_sequence<Is...>) noexcept {
    return (catalog_bijection_at<Is>() && ...);
}

static_assert(catalog_bijection_holds(::std::make_index_sequence<fixy_catalog_size>{}),
              "FixyCatalog ordering drifted from DimensionAxis value ordering. "
              "Each FixyCatalog entry at index I must satisfy "
              "tag_for_axis_t<static_cast<DimensionAxis>(I)> == entry AND "
              "axis_for_tag_v<entry> == static_cast<DimensionAxis>(I).");

}  // namespace detail::fixy_catalog

static_assert(is_fixy_diag_v<FixyNotEngaged_Type>, "FixyNotEngaged_Type must be recognized as a fixy diagnostic.");
static_assert(is_fixy_diag_v<FixyNotEngaged_Staleness>,
              "FixyNotEngaged_Staleness must be recognized as a fixy diagnostic.");
static_assert(!is_fixy_diag_v<int>, "Plain primitive types must not register as fixy diagnostics.");
static_assert(!is_fixy_diag_v<::crucible::safety::diag::tag_base>,
              "tag_base itself is not a catalog entry — only concrete subclasses "
              "appear in FixyCatalog.");
static_assert(!is_fixy_diag_v<::crucible::safety::diag::HotPathViolation>,
              "Substrate diagnostic tags MUST NOT register as fixy diagnostics. "
              "The substrate Catalog and FixyCatalog are disjoint by design.");
static_assert(!is_fixy_diag_v<::crucible::safety::diag::EffectRowMismatch>,
              "Substrate diagnostic tags MUST NOT register as fixy diagnostics.");

static_assert(is_fixy_diag_v<FixyDuplicate_Type>, "FixyDuplicate_Type must be recognized as a fixy "
                                                  "diagnostic (FixyDuplicateCatalog entry).");
static_assert(is_fixy_diag_v<FixyDuplicate_Staleness>, "FixyDuplicate_Staleness must be recognized as a fixy "
                                                       "diagnostic.");
static_assert(is_fixy_diag_v<FixyDuplicate_MemoryScope>,
              "FixyDuplicate_MemoryScope (newest axis dup tag) must "
              "be recognized as a fixy diagnostic — sentinel for the FixyDuplicate "
              "tail growing in lockstep with DimensionAxis appends.");
static_assert(is_fixy_diag_v<FixyMalformedGrant>, "FixyMalformedGrant must be recognized as a fixy "
                                                  "diagnostic (FixyMalformedCatalog entry).");

static_assert(fixy_duplicate_catalog_size == fixy_catalog_size,
              "FixyDuplicateCatalog cardinality drifted from "
              "FixyCatalog.  Adding a new DimensionAxis enumerator requires "
              "(a) the matching FixyNotEngaged_<Axis> tag (via "
              "CRUCIBLE_FIXY_NOT_ENGAGED_TAG), (b) the matching FixyDuplicate_"
              "<Axis> tag (via CRUCIBLE_FIXY_DUPLICATE_TAG), (c) appending BOTH "
              "to their respective catalogs in DimensionAxis order, (d) the "
              "tag_for_axis + dup_tag_for_axis specializations, and (e) the "
              "axis_for_tag specialization.  This assertion fires when (c) was "
              "skipped on the FixyDuplicate side.");

static_assert(fixy_malformed_catalog_size == 1, "FixyMalformedCatalog must contain exactly the "
                                                "singleton FixyMalformedGrant tag.  Adding additional malformed-"
                                                "grant tags is a deliberate redesign — update the catalog AND "
                                                "either route through a richer Diagnose* family or extend "
                                                "AllGrantsWellFormed to discriminate them.");

namespace detail::fixy_catalog {

template <::std::size_t... Is>
[[nodiscard]] consteval bool all_duplicate_fixy_diag(::std::index_sequence<Is...>) noexcept {
    return (is_fixy_diag_v<::std::tuple_element_t<Is, FixyDuplicateCatalog>> && ...);
}

}  // namespace detail::fixy_catalog

static_assert(detail::fixy_catalog::all_duplicate_fixy_diag(::std::make_index_sequence<fixy_duplicate_catalog_size>{}),
              "every FixyDuplicateCatalog entry must satisfy "
              "is_fixy_diag_v.  If this fires, an entry was added to the "
              "catalog but the predicate union was not extended — keep them "
              "in sync at the definition site.");

// A floor, not an equality.  The exact ceiling sits beside the
// substrate constant itself, and the fold below adapts to the number
// of entries that exist.  What this catches is the inverse direction,
// an accidental removal of a substrate catalog entry.

static_assert(::crucible::safety::diag::catalog_size >= 31,
              "safety::diag::catalog_size regressed below "
              "31 — a Catalog entry was removed without updating both "
              "Diagnostic.h's colocated ceiling pin AND this floor witness.");

namespace detail::substrate_disjointness {

template <::std::size_t... Is>
inline constexpr bool no_substrate_in_fixy_catalog(::std::index_sequence<Is...>) noexcept {
    return (!is_fixy_diag_v<::std::tuple_element_t<Is, ::crucible::safety::diag::Catalog>> && ...);
}

}  // namespace detail::substrate_disjointness

static_assert(detail::substrate_disjointness::no_substrate_in_fixy_catalog(
                  ::std::make_index_sequence<::crucible::safety::diag::catalog_size>{}),
              "at least one substrate diagnostic in "
              "safety::diag::Catalog ALSO registers as a fixy diagnostic. "
              "Substrate Catalog and FixyCatalog are disjoint by design.  "
              "The fold walks every entry; a "
              "fixy-side dual-export of a substrate tag would trip here.");

// The fold above walks substrate to fixy.  This one walks fixy to
// itself.  The bijection self-test checks round-trip, not in-tuple
// membership, so neither subsumes the other.

namespace detail::fixy_positive_witness {

template <::std::size_t... Is>
[[nodiscard]] inline constexpr bool every_fixy_entry_is_fixy_diag(::std::index_sequence<Is...>) noexcept {
    return (is_fixy_diag_v<::std::tuple_element_t<Is, FixyCatalog>> && ...);
}

}  // namespace detail::fixy_positive_witness

static_assert(
    detail::fixy_positive_witness::every_fixy_entry_is_fixy_diag(::std::make_index_sequence<fixy_catalog_size>{}),
    "fixy-M-12: at least one FixyCatalog entry does NOT satisfy "
    "is_fixy_diag_v.  The two predicates must agree on every catalog "
    "entry (the tuple IS the source of truth).  A failure here means "
    "is_fixy_diag_v's specialization is missing an entry, OR the "
    "entry was added to FixyCatalog without updating the predicate.");

static_assert(axis_for_tag_v<FixyNotEngaged_Type> == dim::DimensionAxis::Type,
              "axis_for_tag must invert tag_for_axis at the Type axis.");
static_assert(axis_for_tag_v<FixyNotEngaged_Staleness> == dim::DimensionAxis::Staleness,
              "axis_for_tag must invert tag_for_axis at the Staleness axis.");

}  // namespace diag

namespace detail::engagement {

// The inline fold
//
//     ((grant::IsGrantTag_v<G> && grant::which_dim_v<G> == D) || ...)
//
// substitutes `which_dim_v<G>` for every G in the pack before the
// `&&` short-circuits.  `which_dim`'s primary template is undefined,
// so a non-grant G that reaches here ahead of the acceptance gate
// turns a clean rejection into a hard substitution error.  A fold cannot be
// rescued by constraint partial-ordering, so the probe is extracted
// and the lookup gated behind `if constexpr`.
template <dim::DimensionAxis D, typename G>
[[nodiscard]] consteval bool engages_dim_one() noexcept {
    if constexpr (grant::IsGrantTag_v<G>) {
        return grant::which_dim_v<G> == D;
    } else {
        return false;
    }
}

template <dim::DimensionAxis D, typename... Grants>
[[nodiscard]] consteval bool engaged_for() noexcept {
    if constexpr (sizeof...(Grants) == 0) {
        return false;
    } else {
        return (engages_dim_one<D, Grants>() || ...);
    }
}

template <typename... Grants>
[[nodiscard]] consteval bool all_grants_well_formed() noexcept {
    if constexpr (sizeof...(Grants) == 0) {
        return true;
    } else {
        return (grant::IsGrantTag_v<Grants> && ...);
    }
}

// The reflected enumerator span is materialized once and spliced per
// index so the four walks below share it, and no walk has to build a
// static array of its own.

inline constexpr auto kAxisEnumerators =
    std::define_static_array(std::meta::enumerators_of(^^::crucible::safety::DimensionAxis));

inline constexpr std::size_t kAxisCount = kAxisEnumerators.size();

template <std::size_t I>
inline constexpr auto axis_at_v = [:kAxisEnumerators[I]:];

template <std::size_t I, typename... Grants>
[[nodiscard]] consteval std::optional<dim::DimensionAxis> first_missing_axis_impl() noexcept {
    if constexpr (I >= kAxisCount) {
        return std::nullopt;
    } else if constexpr (!engaged_for<axis_at_v<I>, Grants...>()) {
        return axis_at_v<I>;
    } else {
        return first_missing_axis_impl<I + 1, Grants...>();
    }
}

template <typename... Grants>
[[nodiscard]] consteval std::optional<dim::DimensionAxis> first_missing_axis() noexcept {
    return first_missing_axis_impl<0, Grants...>();
}

template <std::size_t I, typename... Grants>
[[nodiscard]] consteval bool every_axis_engaged_impl() noexcept {
    if constexpr (I >= kAxisCount) {
        return true;
    } else if constexpr (!engaged_for<axis_at_v<I>, Grants...>()) {
        return false;
    } else {
        return every_axis_engaged_impl<I + 1, Grants...>();
    }
}

template <typename... Grants>
[[nodiscard]] consteval bool every_axis_engaged() noexcept {
    return every_axis_engaged_impl<0, Grants...>();
}

// Routed through the gated probe for the same reason `engaged_for` is:
// an inline `which_dim_v<G>` would be substituted for every G in the
// pack before the `?:` runs.
template <dim::DimensionAxis D, typename... Grants>
[[nodiscard]] consteval std::size_t count_engagements_for() noexcept {
    if constexpr (sizeof...(Grants) == 0) {
        return 0;
    } else {
        return ((engages_dim_one<D, Grants>() ? 1u : 0u) + ...);
    }
}

template <std::size_t I, typename... Grants>
[[nodiscard]] consteval bool every_axis_engaged_at_most_once_impl() noexcept {
    if constexpr (I >= kAxisCount) {
        return true;
    } else if constexpr (count_engagements_for<axis_at_v<I>, Grants...>() > 1u) {
        return false;
    } else {
        return every_axis_engaged_at_most_once_impl<I + 1, Grants...>();
    }
}

template <typename... Grants>
[[nodiscard]] consteval bool every_axis_engaged_at_most_once() noexcept {
    return every_axis_engaged_at_most_once_impl<0, Grants...>();
}

template <std::size_t I, typename... Grants>
[[nodiscard]] consteval std::optional<dim::DimensionAxis> first_duplicate_axis_impl() noexcept {
    if constexpr (I >= kAxisCount) {
        return std::nullopt;
    } else if constexpr (count_engagements_for<axis_at_v<I>, Grants...>() > 1u) {
        return axis_at_v<I>;
    } else {
        return first_duplicate_axis_impl<I + 1, Grants...>();
    }
}

template <typename... Grants>
[[nodiscard]] consteval std::optional<dim::DimensionAxis> first_duplicate_axis() noexcept {
    return first_duplicate_axis_impl<0, Grants...>();
}

}  // namespace detail::engagement

template <typename... Grants>
concept AllDimsEngaged = detail::engagement::every_axis_engaged<Grants...>();

template <typename... Grants>
concept AllGrantsWellFormed = detail::engagement::all_grants_well_formed<Grants...>();

// A second grant on an axis is silently discarded downstream, where the
// first matching grant wins, so a duplicate loses information without
// saying so.  The rejection here also bans an explicitly written Type
// marker: the wrapper injects that marker itself, so a hand-written one
// arrives as a duplicate on the Type axis.

template <typename... Grants>
concept UniqueEngagementPerAxis = detail::engagement::every_axis_engaged_at_most_once<Grants...>();

template <typename... Grants>
concept IsAcceptedGrants =
    AllGrantsWellFormed<Grants...> && AllDimsEngaged<Grants...> && UniqueEngagementPerAxis<Grants...>;

namespace detail::accept {

// [meta.unary.cat] defines `is_object_v<T>` to exclude reference types,
// function types and cv-qualified void.  The payload predicate below
// leans on that and carries no `!is_reference_v` term of its own.  The
// four asserts pin the standard's guarantee here, so a language change
// that loosened `is_object_v` would redden at this one site instead of
// at every call that trusts the predicate.

static_assert(!std::is_object_v<int&>, "fixy-L-07 invariant: is_object_v must exclude lvalue references "
                                       "(C++ [meta.unary.cat]).  Body of type_is_accepted_payload below "
                                       "drops !is_reference_v on this basis.");

static_assert(!std::is_object_v<int&&>, "fixy-L-07 invariant: is_object_v must exclude rvalue references "
                                        "(C++ [meta.unary.cat]).  Body of type_is_accepted_payload below "
                                        "drops !is_reference_v on this basis.");

static_assert(!std::is_object_v<void>, "fixy-L-07 invariant: is_object_v must exclude void "
                                       "(C++ [meta.unary.cat]).");

static_assert(!std::is_object_v<int(int)>, "fixy-L-07 invariant: is_object_v must exclude bare function "
                                           "types (C++ [meta.unary.cat]).");

template <typename T>
[[nodiscard]] consteval bool type_is_accepted_payload() noexcept {
    // A top-level cv-qualifier silently deletes the wrapper's defaulted
    // copy-assign and move-assign.  An array decays in the wrapper's
    // by-value constructor, so it would alias a pointer where the caller
    // asked for a value copy.  Function pointers and callables are object
    // types and pass.  A bare function type does not.
    return std::is_object_v<T> && !std::is_const_v<T> && !std::is_volatile_v<T> && !std::is_array_v<T>;
}

static_assert(type_is_accepted_payload<int>(), "scalars must be accepted payloads.");
static_assert(type_is_accepted_payload<int*>(), "object pointers must be accepted payloads.");
static_assert(type_is_accepted_payload<int (*)(int)>(),
              "function POINTERS are object types — must be accepted (only bare "
              "function types are rejected).");
static_assert(!type_is_accepted_payload<int(int)>(), "bare function types must be rejected — wrap as function pointer "
                                                     "or callable before instantiating fixy::fn.");
static_assert(!type_is_accepted_payload<void>(), "void must be rejected — Fn<void, ...> has no value-category "
                                                 "semantics.");
static_assert(!type_is_accepted_payload<int&>(), "lvalue references must be rejected.");
static_assert(!type_is_accepted_payload<int&&>(), "rvalue references must be rejected.");
static_assert(!type_is_accepted_payload<const int>(), "top-level const must be rejected — silently deletes Fn's "
                                                      "defaulted assignment ops.");
static_assert(!type_is_accepted_payload<volatile int>(), "top-level volatile must be rejected.");
static_assert(!type_is_accepted_payload<int[5]>(), "arrays must be rejected — Fn(Type) would decay array to pointer.");

// The marker lives here, beside the concept that injects it, so this
// header does not have to depend on the wrapper that resolves it.
using ImplicitTypeMarker = grant::accept_default_strict_for<dim::DimensionAxis::Type>;

}  // namespace detail::accept

// The direct form expects the Type-axis marker already present in the
// pack.  `IsAccepted` below is the form user code writes, and it
// injects that marker, because a hand-written one would land as a
// duplicate on the Type axis.

template <typename Type, typename... Grants>
concept IsAcceptedDirect = detail::accept::type_is_accepted_payload<Type>()
                        && IsAcceptedGrants<Grants...> && theory::NotInTheoryCorpus<Type, Grants...>;

template <typename Type, typename... Grants>
inline constexpr bool IsAcceptedDirect_v = IsAcceptedDirect<Type, Grants...>;

// Composition-collision rules are deliberately not part of this gate.
// The wrapper's own class body checks them when it instantiates the
// substrate, at the same instantiation point that fires this concept.
// Folding them in here would pull the substrate's whole resolver into
// this header and surface no diagnostic the wrapper does not give.

template <typename Type, typename... Grants>
concept IsAccepted = IsAcceptedDirect<Type, detail::accept::ImplicitTypeMarker, Grants...>;

template <typename Type, typename... Grants>
inline constexpr bool IsAccepted_v = IsAccepted<Type, Grants...>;

static_assert(IsAccepted_v<int> == IsAccepted<int>, "fixy-L-08: IsAccepted_v must equal IsAccepted on every pack — "
                                                    "the variable-template is the public-API mirror of the concept.");

static_assert(IsAcceptedDirect_v<int> == IsAcceptedDirect<int>,
              "fixy-L-08: IsAcceptedDirect_v must equal IsAcceptedDirect on every "
              "pack — the variable-template is the public-API mirror of the "
              "low-level concept (IsAcceptedDirect expects a complete pack; both "
              "variable and concept correctly reject the bare-int probe here).");

template <typename... Grants>
inline constexpr std::optional<dim::DimensionAxis> first_missing_axis_v =
    detail::engagement::first_missing_axis<Grants...>();

// Observability derives its strict default from Effect, which invites
// the conclusion that an engagement of Effect engages Observability too.  It
// does not: the derived default governs how the axis resolves, not
// whether the pack has to say something about it, and the engagement
// walk treats every axis alike.  The two witnesses below pin that in
// both directions so an optimization that skips derived axes in the
// walk reddens next to the walk itself.

namespace detail::observability_witness {

template <dim::DimensionAxis A>
using S = ::crucible::fixy::grant::accept_default_strict_for<A>;

using D = dim::DimensionAxis;

// The Type marker is spelled out here because this witness calls the
// engagement walk directly rather than through the wrapper that would
// inject it.
inline constexpr bool observability_diagnostic_is_alive_v =
    detail::engagement::first_missing_axis<
        S<D::Type>, S<D::Refinement>, S<D::Usage>, S<D::Effect>, S<D::Security>, S<D::Protocol>, S<D::Lifetime>,
        S<D::Provenance>, S<D::Trust>, S<D::Representation>,
        /* Observability deliberately omitted */
        S<D::Complexity>, S<D::Precision>, S<D::Space>, S<D::Overflow>, S<D::Mutation>, S<D::Reentrancy>, S<D::Size>,
        S<D::Version>, S<D::Staleness>, S<D::Synchronization>, S<D::Regime>, S<D::FpMode>, S<D::SyscallSurface>,
        S<D::ControlFlow>, S<D::CallShape>, S<D::StackUse>, S<D::GlobalState>, S<D::Stdio>, S<D::HwInstruction>,
        S<D::BarrierStrength>, S<D::SimdIsa>, S<D::MemoryScope>>()
    == D::Observability;

inline constexpr bool every_axis_pack_engages_observability_v =
    !detail::engagement::first_missing_axis<
         S<D::Type>, S<D::Refinement>, S<D::Usage>, S<D::Effect>, S<D::Security>, S<D::Protocol>, S<D::Lifetime>,
         S<D::Provenance>, S<D::Trust>, S<D::Representation>, S<D::Observability>, S<D::Complexity>, S<D::Precision>,
         S<D::Space>, S<D::Overflow>, S<D::Mutation>, S<D::Reentrancy>, S<D::Size>, S<D::Version>, S<D::Staleness>,
         S<D::Synchronization>, S<D::Regime>, S<D::FpMode>, S<D::SyscallSurface>, S<D::ControlFlow>, S<D::CallShape>,
         S<D::StackUse>, S<D::GlobalState>, S<D::Stdio>, S<D::HwInstruction>, S<D::BarrierStrength>, S<D::SimdIsa>,
         S<D::MemoryScope>>()
         .has_value();

}  // namespace detail::observability_witness

static_assert(detail::observability_witness::observability_diagnostic_is_alive_v,
              "fixy-A4-026: the engagement walk MUST surface Observability "
              "as the first missing axis when handed a 20-axis pack omitting "
              "ONLY Observability.  If this fires, someone removed Observability "
              "from `every_axis_engaged()`'s walk — likely under the "
              "(incorrect) assumption that `HasDerivedDefault` auto-engages "
              "the axis.  See the doc-block above and Default.h:299: "
              "HasDerivedDefault only affects how the strict-default resolves "
              "(through Effect), NOT whether engagement is required.  "
              "`accept_default_strict_for<Observability>` is the only legal "
              "engagement; omitting it MUST trip FixyNotEngaged_Observability.");

static_assert(detail::observability_witness::every_axis_pack_engages_observability_v,
              "fixy-A4-026: a Grants pack engaging every axis (including "
              "Observability) MUST yield std::nullopt from first_missing_axis — "
              "the witness above pins the rejection direction; this pin "
              "documents the acceptance direction.  A failure here means "
              "Observability is silently rejected even when engaged, breaking "
              "every production stance alias (Fn.h:1002+, 1514, 1601).");

// The requires-clause is what makes the dereference safe: the alias is
// only nameable on a pack that has a missing axis, so the optional is
// engaged wherever this alias resolves.

template <typename... Grants>
    requires(!AllDimsEngaged<Grants...>)
using first_missing_tag_t = diag::tag_for_axis_t<*first_missing_axis_v<Grants...>>;

// The `if constexpr` is not an optimization.  `first_missing_tag_t` is
// ill-formed on a fully engaged pack, so only the guarded branch may be
// instantiated there.

template <typename... Grants>
inline constexpr std::string_view first_missing_tag_name_v = []() consteval -> std::string_view {
    if constexpr (AllDimsEngaged<Grants...>) {
        return std::string_view{};
    } else {
        return ::crucible::safety::diag::diagnostic_name_v<first_missing_tag_t<Grants...>>;
    }
}();

// `define_static_string` promotes the consteval-built string into
// static storage, so the returned view still points at live bytes once
// the lambda has returned.

template <typename... Grants>
inline constexpr std::string_view tier3_missing_tag_message_v = []() consteval -> std::string_view {
    if constexpr (AllDimsEngaged<Grants...>) {
        return std::string_view{};
    } else {
        constexpr std::string_view tagname = first_missing_tag_name_v<Grants...>;
        std::string msg;
        msg += "fixy::fn<Type, Grants...> [tier 3: IsAccepted gate / "
               "AllDimsEngaged]: at least one DimensionAxis is NOT "
               "engaged by any grant.  Missing-axis diagnostic tag: ";
        msg.append(tagname.data(), tagname.size());
        msg += ".  Either add `grant::accept_default_strict_for"
               "<dim::DimensionAxis::<Axis>>` to accept the strict "
               "default, or supply the appropriate per-axis relaxation "
               "tag from fixy::grant::*.  See fixy::first_missing_axis_v"
               "<Grants...> for the axis enum and "
               "fixy::first_missing_tag_t<Grants...> for the resolved "
               "FixyNotEngaged_<Axis> type.";
        return std::string_view{std::define_static_string(msg)};
    }
}();

template <typename... Grants>
inline constexpr std::optional<dim::DimensionAxis> first_duplicate_axis_v =
    detail::engagement::first_duplicate_axis<Grants...>();

template <typename... Grants>
    requires(!UniqueEngagementPerAxis<Grants...>)
using first_duplicate_tag_t = diag::dup_tag_for_axis_t<*first_duplicate_axis_v<Grants...>>;

template <typename... Grants>
inline constexpr std::string_view first_duplicate_tag_name_v = []() consteval -> std::string_view {
    if constexpr (UniqueEngagementPerAxis<Grants...>) {
        return std::string_view{};
    } else {
        return ::crucible::safety::diag::diagnostic_name_v<first_duplicate_tag_t<Grants...>>;
    }
}();

template <typename... Grants>
inline constexpr std::string_view tier4_duplicate_tag_message_v = []() consteval -> std::string_view {
    if constexpr (UniqueEngagementPerAxis<Grants...>) {
        return std::string_view{};
    } else {
        constexpr std::string_view tagname = first_duplicate_tag_name_v<Grants...>;
        std::string msg;
        msg += "fixy::fn<Type, Grants...> [tier 4: IsAccepted gate / "
               "UniqueEngagementPerAxis]: at least one DimensionAxis "
               "is engaged MORE THAN ONCE by the Grants pack.  "
               "Duplicate-axis diagnostic tag: ";
        msg.append(tagname.data(), tagname.size());
        msg += ".  Remove the redundant grant(s).  See "
               "fixy::first_duplicate_axis_v<Grants...> for the axis "
               "enum and fixy::first_duplicate_tag_t<Grants...> for "
               "the resolved FixyDuplicate_<Axis> type.  Note: "
               "explicitly writing `grant::accept_default_strict_for"
               "<dim::DimensionAxis::Type>` is FORBIDDEN — fixy::fn "
               "implicitly engages Type, so an "
               "explicit Type marker would trigger this duplicate.";
        return std::string_view{std::define_static_string(msg)};
    }
}();

template <typename... Grants>
inline constexpr std::size_t first_malformed_grant_index_v = []() consteval -> std::size_t {
    std::size_t idx = 0;
    bool found = false;
    (void)((!found && (grant::IsGrantTag<Grants> ? (++idx, false) : (found = true))) || ...);
    return found ? idx : sizeof...(Grants);
}();

template <typename... Grants>
inline constexpr std::string_view tier2_malformed_grant_message_v = []() consteval -> std::string_view {
    if constexpr (AllGrantsWellFormed<Grants...>) {
        return std::string_view{};
    } else {
        constexpr std::size_t idx = first_malformed_grant_index_v<Grants...>;
        std::string msg;
        msg += "fixy::fn<Type, Grants...> [tier 2: IsAccepted gate / "
               "AllGrantsWellFormed]: at least one Grants pack entry "
               "is NOT a well-formed grant tag (does not satisfy "
               "fixy::grant::IsGrantTag: must be final-class, must "
               "inherit grant_base, must not be a non-grant type "
               "such as `int` or a user struct).  Malformed-grant "
               "position (0-based): ";
        {
            char buf[32]{};
            auto r = std::to_chars(buf, buf + sizeof(buf), idx);
            msg.append(buf, static_cast<std::size_t>(r.ptr - buf));
        }
        msg += " of ";
        {
            char buf[32]{};
            auto r = std::to_chars(buf, buf + sizeof(buf), sizeof...(Grants));
            msg.append(buf, static_cast<std::size_t>(r.ptr - buf));
        }
        msg += ".  Common causes: copy-paste typo, misspelled grant "
               "name, substrate type accidentally passed where a "
               "grant tag was expected, or extending a non-final "
               "grant class.  See fixy::diag::FixyMalformedGrant "
               "for the structured diagnostic tag.";
        return std::string_view{std::define_static_string(msg)};
    }
}();

// A static_assert message is a string literal, so a tag named inside
// one never puts the resolved tag's class name in front of the reader.
// Instantiating the tag as a template argument does: the helper's name,
// with the tag spelled out in it, lands in the compiler's "required
// from" trail.  Each helper pairs a primary template that fires with
// an empty `void` specialization, and the wrapper instantiates it on a
// type that resolves to `void` when the tier passes.
//
// The tier conditions are chained so that a later tier stays silent
// while an earlier one still fails.  Missing-axis is meaningless on a
// pack that does not consist of grants in the first place.

namespace detail::diagnose {

template <typename T>
inline constexpr bool always_false_v = false;

// A partial specialization rather than `conditional_t`: the failure
// branch is ill-formed when the tier passes, and `conditional_t` would
// substitute it anyway.

template <bool Failed, typename... Grants>
struct select_missing_tag {
    using type = void;
};

template <typename... Grants>
struct select_missing_tag<true, Grants...> {
    // Failed is true only when an axis is missing, so the optional is
    // engaged and `.value()` stays a constant expression.
    using type = diag::tag_for_axis_t<detail::engagement::first_missing_axis<Grants...>().value()>;
};

template <bool Failed, typename... Grants>
struct select_duplicate_tag {
    using type = void;
};

template <typename... Grants>
struct select_duplicate_tag<true, Grants...> {
    // Failed is true only when an axis is engaged more than once, so
    // the optional is engaged and `.value()` stays a constant expression.
    using type = diag::dup_tag_for_axis_t<detail::engagement::first_duplicate_axis<Grants...>().value()>;
};

}  // namespace detail::diagnose

template <typename... Grants>
using malformed_grant_or_void_t = std::conditional_t<AllGrantsWellFormed<Grants...>, void, diag::FixyMalformedGrant>;

template <typename... Grants>
using missing_tag_or_void_t =
    typename detail::diagnose::select_missing_tag<AllGrantsWellFormed<Grants...> && !AllDimsEngaged<Grants...>,
                                                  Grants...>::type;

template <typename... Grants>
using duplicate_tag_or_void_t =
    typename detail::diagnose::select_duplicate_tag<AllGrantsWellFormed<Grants...> && AllDimsEngaged<Grants...>
                                                        && !UniqueEngagementPerAxis<Grants...>,
                                                    Grants...>::type;

template <typename Tag>
struct DiagnoseAxisNotEngaged {
    static_assert(detail::diagnose::always_false_v<Tag>,
                  "fixy::fn<Type, Grants...>: AllDimsEngaged FAILED — the Tag "
                  "template parameter on this DiagnoseAxisNotEngaged<...> "
                  "instantiation names the specific FixyNotEngaged_<Axis> "
                  "diagnostic tag for the offending axis.  Add the matching "
                  "`grant::accept_default_strict_for<dim::DimensionAxis::<Axis>>` "
                  "or a per-axis relaxation tag from fixy::grant::*.");
};

template <>
struct DiagnoseAxisNotEngaged<void> {};

template <typename Tag>
struct DiagnoseAxisDuplicate {
    static_assert(detail::diagnose::always_false_v<Tag>,
                  "fixy::fn<Type, Grants...>: UniqueEngagementPerAxis FAILED — "
                  "the Tag template parameter on this DiagnoseAxisDuplicate<...> "
                  "instantiation names the specific FixyDuplicate_<Axis> "
                  "diagnostic tag for the duplicated axis.  Remove the redundant "
                  "grant(s) for that axis.");
};

template <>
struct DiagnoseAxisDuplicate<void> {};

template <typename Tag>
struct DiagnoseMalformedGrant {
    static_assert(detail::diagnose::always_false_v<Tag>, "fixy::fn<Type, Grants...>: AllGrantsWellFormed FAILED — the "
                                                         "Tag template parameter on this DiagnoseMalformedGrant<...> "
                                                         "instantiation names FixyMalformedGrant.  The Grants pack "
                                                         "contains an entry that does NOT satisfy fixy::grant::"
                                                         "IsGrantTag (not final-class, doesn't inherit grant_base, or "
                                                         "is a non-grant type entirely such as `int`).");
};

template <>
struct DiagnoseMalformedGrant<void> {};

namespace detail::reject_self_test {

template <dim::DimensionAxis D>
using strict = grant::accept_default_strict_for<D>;

using AllStrictPack = std::tuple<
    strict<dim::DimensionAxis::Type>, strict<dim::DimensionAxis::Refinement>, strict<dim::DimensionAxis::Usage>,
    strict<dim::DimensionAxis::Effect>, strict<dim::DimensionAxis::Security>, strict<dim::DimensionAxis::Protocol>,
    strict<dim::DimensionAxis::Lifetime>, strict<dim::DimensionAxis::Provenance>, strict<dim::DimensionAxis::Trust>,
    strict<dim::DimensionAxis::Representation>, strict<dim::DimensionAxis::Observability>,
    strict<dim::DimensionAxis::Complexity>, strict<dim::DimensionAxis::Precision>, strict<dim::DimensionAxis::Space>,
    strict<dim::DimensionAxis::Overflow>, strict<dim::DimensionAxis::Mutation>, strict<dim::DimensionAxis::Reentrancy>,
    strict<dim::DimensionAxis::Size>, strict<dim::DimensionAxis::Version>, strict<dim::DimensionAxis::Staleness>,
    strict<dim::DimensionAxis::Synchronization>, strict<dim::DimensionAxis::Regime>, strict<dim::DimensionAxis::FpMode>,
    strict<dim::DimensionAxis::SyscallSurface>, strict<dim::DimensionAxis::ControlFlow>,
    strict<dim::DimensionAxis::CallShape>, strict<dim::DimensionAxis::StackUse>,
    strict<dim::DimensionAxis::GlobalState>, strict<dim::DimensionAxis::Stdio>,
    strict<dim::DimensionAxis::HwInstruction>, strict<dim::DimensionAxis::BarrierStrength>,
    strict<dim::DimensionAxis::SimdIsa>, strict<dim::DimensionAxis::MemoryScope>>;

template <template <typename...> class Tmpl, typename Tuple>
struct apply_tuple;
template <template <typename...> class Tmpl, typename... Ts>
struct apply_tuple<Tmpl, std::tuple<Ts...>> {
    using type = Tmpl<Ts...>;
};

// The direct form, because the packs below spell the Type marker out.
// Routing them through the injecting form would engage Type twice and
// trip the uniqueness gate instead of the intended test.
template <typename T, typename Tuple>
inline constexpr bool accepts_pack_v = []() {
    return [&]<typename... Ts>(std::tuple<Ts...>*) consteval {
        return IsAcceptedDirect<T, Ts...>;
    }(static_cast<Tuple*>(nullptr));
}();

static_assert(!IsAccepted<int>, "Empty Grants pack must reject (only Type engaged via injection).");
static_assert(!IsAcceptedGrants<>, "IsAcceptedGrants<> must reject the empty pack.");

// The two asserts above would also hold if the payload type or the
// marker injection were broken.  The next two pin the cause to the
// unengaged axes.
static_assert(first_missing_axis_v<> == dim::DimensionAxis::Type,
              "fixy-M-06: empty Grants pack must miss Type FIRST — pins the "
              "IsAcceptedGrants<> rejection cause as 'no axis engaged'.");
static_assert(first_missing_axis_v<detail::accept::ImplicitTypeMarker> == dim::DimensionAxis::Refinement,
              "fixy-M-06: after Type-marker injection (what IsAccepted does), "
              "the first missing axis MUST be Refinement.  Pins the "
              "IsAccepted<int> rejection cause as 'axes 2..22 unengaged', "
              "NOT 'Type axis' or 'int payload'.  A change to the Type-axis "
              "logic that silently shifts the cause fires this static_assert.");

static_assert(accepts_pack_v<int, AllStrictPack>, "AllStrict pack must accept — every dim has an engagement marker.");

static_assert(!IsAccepted<int, grant::copy>, "Single Usage relaxation must reject — 20 other dims unengaged.");

using CopyForUsagePack = std::tuple<
    strict<dim::DimensionAxis::Type>, strict<dim::DimensionAxis::Refinement>,
    grant::copy,  // relaxation in place of accept-strict on Usage
    strict<dim::DimensionAxis::Effect>, strict<dim::DimensionAxis::Security>, strict<dim::DimensionAxis::Protocol>,
    strict<dim::DimensionAxis::Lifetime>, strict<dim::DimensionAxis::Provenance>, strict<dim::DimensionAxis::Trust>,
    strict<dim::DimensionAxis::Representation>, strict<dim::DimensionAxis::Observability>,
    strict<dim::DimensionAxis::Complexity>, strict<dim::DimensionAxis::Precision>, strict<dim::DimensionAxis::Space>,
    strict<dim::DimensionAxis::Overflow>, strict<dim::DimensionAxis::Mutation>, strict<dim::DimensionAxis::Reentrancy>,
    strict<dim::DimensionAxis::Size>, strict<dim::DimensionAxis::Version>, strict<dim::DimensionAxis::Staleness>,
    strict<dim::DimensionAxis::Synchronization>, strict<dim::DimensionAxis::Regime>, strict<dim::DimensionAxis::FpMode>,
    strict<dim::DimensionAxis::SyscallSurface>, strict<dim::DimensionAxis::ControlFlow>,
    strict<dim::DimensionAxis::CallShape>, strict<dim::DimensionAxis::StackUse>,
    strict<dim::DimensionAxis::GlobalState>, strict<dim::DimensionAxis::Stdio>,
    strict<dim::DimensionAxis::HwInstruction>, strict<dim::DimensionAxis::BarrierStrength>,
    strict<dim::DimensionAxis::SimdIsa>, strict<dim::DimensionAxis::MemoryScope>>;

static_assert(accepts_pack_v<int, CopyForUsagePack>, "Replacing accept-strict<Usage> with `grant::copy` must still "
                                                     "accept — `copy` engages the Usage axis.");

using MinusEffectPack = std::tuple<
    strict<dim::DimensionAxis::Type>, strict<dim::DimensionAxis::Refinement>, strict<dim::DimensionAxis::Usage>,
    // Effect removed
    strict<dim::DimensionAxis::Security>, strict<dim::DimensionAxis::Protocol>, strict<dim::DimensionAxis::Lifetime>,
    strict<dim::DimensionAxis::Provenance>, strict<dim::DimensionAxis::Trust>,
    strict<dim::DimensionAxis::Representation>, strict<dim::DimensionAxis::Observability>,
    strict<dim::DimensionAxis::Complexity>, strict<dim::DimensionAxis::Precision>, strict<dim::DimensionAxis::Space>,
    strict<dim::DimensionAxis::Overflow>, strict<dim::DimensionAxis::Mutation>, strict<dim::DimensionAxis::Reentrancy>,
    strict<dim::DimensionAxis::Size>, strict<dim::DimensionAxis::Version>, strict<dim::DimensionAxis::Staleness>,
    strict<dim::DimensionAxis::Synchronization>, strict<dim::DimensionAxis::Regime>, strict<dim::DimensionAxis::FpMode>,
    strict<dim::DimensionAxis::SyscallSurface>, strict<dim::DimensionAxis::ControlFlow>,
    strict<dim::DimensionAxis::CallShape>, strict<dim::DimensionAxis::StackUse>,
    strict<dim::DimensionAxis::GlobalState>, strict<dim::DimensionAxis::Stdio>,
    strict<dim::DimensionAxis::HwInstruction>, strict<dim::DimensionAxis::BarrierStrength>,
    strict<dim::DimensionAxis::SimdIsa>, strict<dim::DimensionAxis::MemoryScope>>;

static_assert(!accepts_pack_v<int, MinusEffectPack>, "Removing accept-strict<Effect> without replacement must reject.");

// The five asserts below pin the injecting form against the direct one
// on a pack that omits Type.  Taken together they distinguish "some
// marker is injected" from "the Type marker is injected": if the
// injected marker were swapped for another, the injecting form would
// still accept, but the direct form handed an explicit Type marker
// would start rejecting on a duplicate, and the two would disagree.

using MinusTypePack = std::tuple<
    // Type removed
    strict<dim::DimensionAxis::Refinement>, strict<dim::DimensionAxis::Usage>, strict<dim::DimensionAxis::Effect>,
    strict<dim::DimensionAxis::Security>, strict<dim::DimensionAxis::Protocol>, strict<dim::DimensionAxis::Lifetime>,
    strict<dim::DimensionAxis::Provenance>, strict<dim::DimensionAxis::Trust>,
    strict<dim::DimensionAxis::Representation>, strict<dim::DimensionAxis::Observability>,
    strict<dim::DimensionAxis::Complexity>, strict<dim::DimensionAxis::Precision>, strict<dim::DimensionAxis::Space>,
    strict<dim::DimensionAxis::Overflow>, strict<dim::DimensionAxis::Mutation>, strict<dim::DimensionAxis::Reentrancy>,
    strict<dim::DimensionAxis::Size>, strict<dim::DimensionAxis::Version>, strict<dim::DimensionAxis::Staleness>,
    strict<dim::DimensionAxis::Synchronization>, strict<dim::DimensionAxis::Regime>, strict<dim::DimensionAxis::FpMode>,
    strict<dim::DimensionAxis::SyscallSurface>, strict<dim::DimensionAxis::ControlFlow>,
    strict<dim::DimensionAxis::CallShape>, strict<dim::DimensionAxis::StackUse>,
    strict<dim::DimensionAxis::GlobalState>, strict<dim::DimensionAxis::Stdio>,
    strict<dim::DimensionAxis::HwInstruction>, strict<dim::DimensionAxis::BarrierStrength>,
    strict<dim::DimensionAxis::SimdIsa>, strict<dim::DimensionAxis::MemoryScope>>;

template <typename T, typename Tuple>
inline constexpr bool accepts_through_wrapper_v = []() {
    return [&]<typename... Ts>(std::tuple<Ts...>*) consteval {
        return IsAccepted<T, Ts...>;
    }(static_cast<Tuple*>(nullptr));
}();

template <typename T, typename Marker, typename Tuple>
inline constexpr bool accepts_pack_with_marker_v = []() {
    return [&]<typename... Ts>(std::tuple<Ts...>*) consteval {
        return IsAcceptedDirect<T, Marker, Ts...>;
    }(static_cast<Tuple*>(nullptr));
}();

static_assert(
    std::is_same_v<detail::accept::ImplicitTypeMarker, grant::accept_default_strict_for<dim::DimensionAxis::Type>>,
    "detail::accept::ImplicitTypeMarker MUST be the "
    "canonical accept_default_strict_for<Type>.  The IsAccepted "
    "wrapper-discipline relies on this identity — if the marker is "
    "swapped to another type, every IsAccepted call site silently "
    "shifts which axis the auto-injection engages.");

static_assert(grant::which_dim_v<detail::accept::ImplicitTypeMarker> == dim::DimensionAxis::Type,
              "ImplicitTypeMarker MUST project to DimensionAxis"
              "::Type via which_dim.  A change to either the marker's "
              "underlying type OR the which_dim specialization for "
              "accept_default_strict_for<Type> fires this assert.");

static_assert(accepts_through_wrapper_v<int, MinusTypePack>,
              "IsAccepted MUST accept a 32-non-Type-axis pack "
              "because the wrapper auto-injects the Type marker.  If this "
              "fires, the wrapper's Type-marker injection is broken OR a new "
              "axis was added without updating MinusTypePack.");

static_assert(!accepts_pack_v<int, MinusTypePack>, "IsAcceptedDirect MUST REJECT a 32-non-Type-axis "
                                                   "pack — Type is missing.  If this fires, IsAcceptedDirect "
                                                   "is silently injecting a marker (defeating its 'you must know "
                                                   "what you're doing' contract).");

static_assert(accepts_pack_with_marker_v<int, detail::accept::ImplicitTypeMarker, MinusTypePack>,
              "IsAcceptedDirect WITH an explicit Type marker "
              "MUST accept the same pack — proves IsAccepted's auto-injection "
              "produces a result IDENTICAL to manual marker placement.  If "
              "this fires, the marker's structural identity diverged from the "
              "wrapper's injection path.");

static_assert(!IsAccepted<void, grant::accept_default_strict_for<dim::DimensionAxis::Usage>>,
              "Type=void must reject (Fn requires complete object type).");
static_assert(!IsAccepted<int&>, "Type=int& must reject (no reference types).");
static_assert(!IsAccepted<int[4]>, "Type=int[4] must reject (array decay would corrupt copy ctor).");
static_assert(!IsAccepted<const int>, "Type=const int must reject (silent deletion of assignment).");

static_assert(first_missing_axis_v<> == dim::DimensionAxis::Type,
              "An empty Grants pack reports Type as the first missing axis.");

using MinusRefinementPack = std::tuple<
    strict<dim::DimensionAxis::Type>,
    // Refinement removed
    strict<dim::DimensionAxis::Usage>, strict<dim::DimensionAxis::Effect>, strict<dim::DimensionAxis::Security>,
    strict<dim::DimensionAxis::Protocol>, strict<dim::DimensionAxis::Lifetime>, strict<dim::DimensionAxis::Provenance>,
    strict<dim::DimensionAxis::Trust>, strict<dim::DimensionAxis::Representation>,
    strict<dim::DimensionAxis::Observability>, strict<dim::DimensionAxis::Complexity>,
    strict<dim::DimensionAxis::Precision>, strict<dim::DimensionAxis::Space>, strict<dim::DimensionAxis::Overflow>,
    strict<dim::DimensionAxis::Mutation>, strict<dim::DimensionAxis::Reentrancy>, strict<dim::DimensionAxis::Size>,
    strict<dim::DimensionAxis::Version>, strict<dim::DimensionAxis::Staleness>,
    strict<dim::DimensionAxis::Synchronization>, strict<dim::DimensionAxis::Regime>, strict<dim::DimensionAxis::FpMode>,
    strict<dim::DimensionAxis::SyscallSurface>, strict<dim::DimensionAxis::ControlFlow>,
    strict<dim::DimensionAxis::CallShape>, strict<dim::DimensionAxis::StackUse>,
    strict<dim::DimensionAxis::GlobalState>, strict<dim::DimensionAxis::Stdio>,
    strict<dim::DimensionAxis::HwInstruction>, strict<dim::DimensionAxis::BarrierStrength>,
    strict<dim::DimensionAxis::SimdIsa>, strict<dim::DimensionAxis::MemoryScope>>;

inline constexpr std::optional<dim::DimensionAxis> first_missing_for_minus_refinement = []() consteval {
    return [&]<typename... Ts>(std::tuple<Ts...>*) consteval {
        return first_missing_axis_v<Ts...>;
    }(static_cast<MinusRefinementPack*>(nullptr));
}();

static_assert(first_missing_for_minus_refinement == dim::DimensionAxis::Refinement,
              "first_missing_axis_v points at Refinement when only that axis "
              "is omitted from an otherwise full strict pack.");

using AllAxesStrictPack = std::tuple<
    strict<dim::DimensionAxis::Type>, strict<dim::DimensionAxis::Refinement>, strict<dim::DimensionAxis::Usage>,
    strict<dim::DimensionAxis::Effect>, strict<dim::DimensionAxis::Security>, strict<dim::DimensionAxis::Protocol>,
    strict<dim::DimensionAxis::Lifetime>, strict<dim::DimensionAxis::Provenance>, strict<dim::DimensionAxis::Trust>,
    strict<dim::DimensionAxis::Representation>, strict<dim::DimensionAxis::Observability>,
    strict<dim::DimensionAxis::Complexity>, strict<dim::DimensionAxis::Precision>, strict<dim::DimensionAxis::Space>,
    strict<dim::DimensionAxis::Overflow>, strict<dim::DimensionAxis::Mutation>, strict<dim::DimensionAxis::Reentrancy>,
    strict<dim::DimensionAxis::Size>, strict<dim::DimensionAxis::Version>, strict<dim::DimensionAxis::Staleness>,
    strict<dim::DimensionAxis::Synchronization>, strict<dim::DimensionAxis::Regime>, strict<dim::DimensionAxis::FpMode>,
    strict<dim::DimensionAxis::SyscallSurface>, strict<dim::DimensionAxis::ControlFlow>,
    strict<dim::DimensionAxis::CallShape>, strict<dim::DimensionAxis::StackUse>,
    strict<dim::DimensionAxis::GlobalState>, strict<dim::DimensionAxis::Stdio>,
    strict<dim::DimensionAxis::HwInstruction>, strict<dim::DimensionAxis::BarrierStrength>,
    strict<dim::DimensionAxis::SimdIsa>, strict<dim::DimensionAxis::MemoryScope>>;

inline constexpr std::optional<dim::DimensionAxis> first_missing_for_full_strict_pack = []() consteval {
    return [&]<typename... Ts>(std::tuple<Ts...>*) consteval {
        return first_missing_axis_v<Ts...>;
    }(static_cast<AllAxesStrictPack*>(nullptr));
}();

static_assert(!first_missing_for_full_strict_pack.has_value(),
              "first_missing_axis_v on a fully engaged Grants pack must yield "
              "std::nullopt — fixy-H-08 type-system leak elimination.");

}  // namespace detail::reject_self_test

}  // namespace crucible::fixy
