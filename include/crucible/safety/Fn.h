#pragma once

// ── crucible::safety — Fn.h (Phase 0 P0-1) ──────────────────────────
//
// The unified Fn<...> aggregator template that bundles a value with
// its 20-axis grade vector per fixy.md §24.1.  Every Fixy binding
// ultimately compiles to an `Fn<Type, ...>` instantiation; the
// substrate enforces zero runtime cost (`sizeof(Fn<int>) ==
// sizeof(int)`) so the entire dispatch vocabulary lives in the type
// system.
//
//   Aggregates: 19 positional template parameters (the 20 fixy
//               dimensions minus dim 11 Observability, which is
//               derived from EffectRow per fixy.md §24.1)
//   Tier basis: routes through DimensionTraits.h (P0-3, #1094) —
//               TierKind / DimensionAxis / tier_of_axis<D>
//   ValidComposition gate: routes through safety/CollisionCatalog.h
//               (P0-2, #1096).  The 12 §6.8 collision rules fire at
//               Fn instantiation time, including direct
//               `Fn<X, BadGrades...>` construction that bypasses
//               `mint_fn`.
//   Universal mint pattern: `mint_fn(value)` per CLAUDE.md §XXI
//               (token mint flavor — derives authority from the
//               caller-supplied per-axis grade pack, no Ctx)
//
// ── Dimension catalog (fixy.md §24.1) ──────────────────────────────
//
//   #  Tier  Param name      Default                       Notes
//   ── ────  ──────────────  ───────────────────────────── ─────────
//    1  F    Type            (required)                    fn arg
//    2  F    Refinement      pred::True                    EBO
//    3  S    Usage           UsageMode::Linear             enum
//    4  S    EffectRow       effects::Row<>                empty row
//    5  S    Security        SecLevel::Classified          enum
//    6  T    Protocol        proto::None                   EBO
//    7  S    Lifetime        lifetime::Static              EBO
//    8  S    Source          source::FromInternal          re-export
//    9  S    Trust           trust::Unverified             re-export (FIXY-FOUND-034)
//   10  L    Repr            ReprKind::Opaque              enum
//   11  S    (Observability) — derived from EffectRow      not stored
//   12  —    (FX dim 12 Clock Domain dropped per §24.1)
//   13  S    Cost            cost::Unstated                EBO
//   14  S    Precision       precision::Exact              EBO
//   15  S    Space           space::Zero                   EBO
//   16  S    Overflow        OverflowMode::Trap            enum
//   17  —    (FX dim 17 FP Order dropped per §24.1)
//   18  S    Mutation        MutationMode::Immutable       enum
//   19  S    Reentrancy      ReentrancyMode::NonReentrant  enum
//   20  S    Size            size::Unstated                EBO
//   21  V    Version         1                             u32
//   22  S    Staleness       stale::Fresh                  EBO
//
// ── Storage discipline (zero-runtime-cost) ──────────────────────────
//
// Fn<...> carries EXACTLY ONE runtime member: `Type value_`.  All
// other dimension grades are type-level metadata accessible via
// static member aliases (`type_t`, `usage_v`, `effect_row_t`, …) and
// do not contribute to sizeof.  This means:
//
//   sizeof(Fn<int>)                                 == sizeof(int)
//   sizeof(Fn<int, pred::True, UsageMode::Affine>)  == sizeof(int)
//   sizeof(Fn<std::array<float, 4>>)                == sizeof(std::array<float, 4>)
//
// Static layout invariants below pin this contract.  A regression
// where a future maintainer adds a per-axis runtime member would
// fail the sentinel TU build at the static_assert site, not in
// production downstream.
//
// ── Why not Graded<M, L, T>? ────────────────────────────────────────
//
// Graded<M, L, T> models a SINGLE-AXIS grade.  Fn<...> is a 19-axis
// product.  Naively wrapping each axis as Graded<...> would produce
// a 19-deep nest with redundant metadata at each level.  The
// canonical wrapper-nesting order (CLAUDE.md §XVI) IS the algebraic
// product, but it is ergonomically toxic at the binding-declaration
// level (a 19-line type expression for a single function).  Fn<...>
// is the FACADE over the product: one positional template
// parameter per axis with sane defaults; the substrate's underlying
// Graded<...> machinery powers the per-axis composition.
//
// The wrapper-nesting form REMAINS authoritative for cross-axis
// hash folding (row_hash, FOUND-I02); Fn<...> exposes the same
// hash via reflection-driven traversal of its per-axis members,
// matching the canonical-order fold so federation cache keys are
// consistent regardless of whether a value is materialized as
// `Fn<int, ...>` or `HotPath<Hot, DetSafe<Pure, Computation<R, int>>>`.
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   InitSafe   — `Type value_` is the only runtime member; if T has
//                NSDMI, the wrapper inherits it; if not, the wrapper
//                MUST be brace-initialized (the explicit ctor takes
//                T by value).
//   TypeSafe   — every per-axis enum is `enum class : uint8_t`; the
//                accessor `usage_v` returns the strong type, no
//                implicit conversion to the underlying integer.
//   NullSafe   — no pointer members.
//   MemSafe    — defaulted copy/move; T's move semantics carry
//                through; no allocations performed by Fn.
//   BorrowSafe — no mutation of cross-cutting state.
//   ThreadSafe — no atomics; the per-thread soundness of an Fn is
//                exactly the per-thread soundness of T.
//   LeakSafe   — no resources owned beyond T.
//   DetSafe    — every operation is constexpr; the 19-axis grade is
//                a STATIC property of the binding.
//
// ── Runtime cost ────────────────────────────────────────────────────
//
// `sizeof(Fn<T, ...>) == sizeof(T)` — pinned by the static_assert
// block.  Construction cost = T's construction cost.  Moves +
// copies = T's moves + copies.  Accessor template aliases compile
// to immediate values under -O3.  The 19-axis grade vector imposes
// ZERO bytes of runtime overhead.
//
// ── References ────────────────────────────────────────────────────
//
//   misc/fixy.md §24.1            — the 20-dimension grade vector
//   misc/fixy.md §24.2            — §6.8 ValidComposition catalog
//   misc/02_05_2026.md            — Phase 0 commitment (P0-1 row)
//   safety/DimensionTraits.h      — Tier S/L/T/F/V dispatch (P0-3)
//   safety/Tagged.h               — source::* and trust::* re-exports
//   effects/EffectRow.h           — effects::Row<...> for dim 4
//   CLAUDE.md §XXI                — universal mint pattern
//   CLAUDE.md §XVI                — canonical wrapper-nesting order

#include <crucible/safety/DimensionTraits.h>
#include <crucible/safety/Tagged.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>

#include <concepts>
#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety::fn {

// ═════════════════════════════════════════════════════════════════════
// ── Per-axis vocabulary (Phase 0 P0-1) ─────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Each axis's vocabulary lives in its own sub-namespace so the
// production callsite reads `fn::source::FromUser`, `fn::stale::Fresh`,
// etc.  Where the substrate already ships an authoritative type
// (Tagged.h source/trust, effects::Row), Fn re-exports it; where
// the substrate has no type yet, Fn ships a minimal placeholder.
// The placeholders are EMPTY (zero-state) on purpose — they exist
// only as type-level dispatch tags.

// ── Dim 2 Refinement (Tier-F) — predicate carrier ──────────────────
namespace pred {
    struct True {
        template <typename T>
        [[nodiscard]] static constexpr bool check(const T&) noexcept { return true; }
    };
}

// ── Dim 3 Usage (Tier-S) ───────────────────────────────────────────
enum class UsageMode : std::uint8_t {
    Linear = 0,   // exactly-once consumption (default)
    Affine = 1,   // at-most-once
    Copy   = 2,   // copyable, non-linear
    Ghost      = 3,  // erased at codegen, ghost-only
    Borrow     = 4,  // non-owning borrow capture
    Capability = 5,  // ephemeral external capability
};

// ── Dim 5 Security (Tier-S) ────────────────────────────────────────
enum class SecLevel : std::uint8_t {
    Unclassified = 0,  // freely observable
    Public       = 1,  // observable
    Internal     = 2,  // organization-internal
    Classified   = 3,  // default — observation requires declassify
    Secret       = 4,  // top of lattice — never declassified
};

// ── Dim 6 Protocol (Tier-T) ────────────────────────────────────────
namespace proto {
    struct None {};   // no protocol obligation (default)
}

// ── Dim 7 Lifetime (Tier-S) ────────────────────────────────────────
namespace lifetime {
    struct Static {};                     // valid for the entire program (default)
    template <auto RegionTag> struct In {};  // valid within a named region
}

// ── Dim 8 Provenance (Tier-S) — re-export Tagged.h source::* ───────
//
// We use `safety::source::FromInternal` as the default tag (matches
// fixy.md spec's `source::Internal` semantically — the existing
// substrate already established the `From*` naming convention).
namespace source = crucible::safety::source;

// ── Dim 9 Trust (Tier-S) — re-export Tagged.h trust::* ─────────────
namespace trust = crucible::safety::trust;

// ── Dim 10 Representation (Tier-L) ─────────────────────────────────
enum class ReprKind : std::uint8_t {
    Opaque  = 0,  // layout opaque — default
    C       = 1,  // repr(C) — standard layout
    Packed  = 2,  // repr(packed) — no padding
    Aligned = 3,  // repr(align(N)) — alignment hint
    Simd    = 4,  // SIMD-vector layout
    Atomic  = 5,  // atomic representation / CAS-capable carrier
};

// ── Dim 13 Complexity (Tier-S) — cost annotation ──────────────────
namespace cost {
    struct Unstated {};                     // default — must declare unbounded
    struct Constant {};                     // O(1)
    template <auto N> struct Linear {};     // O(N)
    template <auto N> struct Quadratic {};  // O(N^2)
    struct Unbounded {};                    // explicit unbounded
}

// ── Dim 14 Precision (Tier-S) — FP error bound ────────────────────
namespace precision {
    struct Exact {};                        // default — bit-exact
    struct F32 {};
    struct F64 {};
    template <auto Bound> struct Higham {}; // Higham bound
}

// ── Dim 15 Space (Tier-S) — allocation bound ──────────────────────
namespace space {
    struct Zero {};                          // default — stack only
    struct Unbounded {};
    template <auto N> struct Bounded {};
}

// ── Dim 16 Overflow (Tier-S) ──────────────────────────────────────
enum class OverflowMode : std::uint8_t {
    Trap     = 0,  // default — abort on overflow
    Wrap     = 1,  // 2^N modular
    Saturate = 2,  // clamp to T's range
    Widen    = 3,  // widen result type
};

// ── Dim 18 Mutation (Tier-S) ──────────────────────────────────────
enum class MutationMode : std::uint8_t {
    Immutable = 0,  // default — no in-place mutation
    Mutable   = 1,  // arbitrary in-place mutation permitted
    Append    = 2,  // append-only mutation
    Monotonic = 3,  // monotonic-advance mutation only
};

// ── Dim 19 Reentrancy (Tier-S) ────────────────────────────────────
enum class ReentrancyMode : std::uint8_t {
    NonReentrant = 0,  // default — self-call rejected
    Reentrant    = 1,  // self-call permitted
    Coroutine    = 2,  // suspendable, resumable
};

// ── Dim 20 Size (Tier-S) — codata observation depth ───────────────
namespace size_pol {
    struct Unstated {};                       // default — must declare
    template <auto Depth> struct Sized {};
    struct Productive {};                     // codata
}

// ── Dim 22 Staleness (Tier-S) ──────────────────────────────────────
namespace stale {
    struct Fresh {};                          // default — τ = 0
    template <auto TauMax> struct Stale {};
}

// ── Dim 23 Synchronization (Tier-S, Crucible extension 2026-05-18) ─
//
// Wrapper-only axis — there is NO Fn<...> template-parameter slot for
// it.  safety::Wait<Strategy, T> and safety::MemOrder<Tag, T> wrap a
// value with its waiting-strategy or C++ memory-order discipline at
// the call site; Fn<...> never aggregates them, because the choice is
// per-value, not per-binding.  The sole reason this namespace exists
// is so fixy/Default.h can give the Synchronization axis a `type`
// alias that satisfies the `every_axis_resolves` reflection-driven
// coverage check.
//
// `Unconstrained` IS the strict default — the binding makes no claim
// about Wait strategy or memory-order discipline.  A binding that
// wants to enforce SpinPause / SeqCst / etc. wraps the relevant value
// in safety::Wait or safety::MemOrder at the call site; the fixy::fn
// engagement marker for this axis is therefore `accept_default_strict
// _for<DimensionAxis::Synchronization>` — i.e., "I'm not claiming any
// wrapper here; the wrapper, if any, lives on the value itself."
namespace sync {
    struct Unconstrained {};   // default — no sync wrapper claim at binding scope
}

// ── Dim 24 Regime (Tier-S, Crucible extension 2026-05-18) ──────────
//
// Wrapper-only axis — there is NO Fn<...> template-parameter slot for
// it.  safety::HotPath<Tier, T> wraps a value with its operating-
// regime tier (Hot / Warm / Cold) at the call site; Fn<...> never
// aggregates it, because the choice is per-value, not per-binding.
// The sole reason this namespace exists is so fixy/Default.h can give
// the Regime axis a `type` alias that satisfies the `every_axis_
// resolves` reflection-driven coverage check.
//
// `Unconstrained` IS the strict default — the binding makes no claim
// about operating regime.  A binding that wants to enforce Hot / Warm
// / Cold wraps the relevant value in safety::HotPath at the call site;
// the fixy::fn engagement marker for this axis is therefore
// `accept_default_strict_for<DimensionAxis::Regime>` — i.e., "I'm not
// claiming any wrapper here; the wrapper, if any, lives on the value
// itself."
namespace regime {
    struct Unconstrained {};   // default — no regime wrapper claim at binding scope
}

// ── Dim 22 FpMode (Tier-S, FIXY-V-088, 2026-05-22) ─────────────────
//
// Wrapper-only axis — there is NO Fn<...> template-parameter slot for
// it.  The FpMode taxonomy lives in algebra/lattices/FpModeLattice.h
// (11 sub-axes: Rounding / Ftz / Contract / TrapMask / Denormal /
// NanPolicy / InfPolicy / ComplexLayout / LibmPolicy / Reassociate /
// FpConstant); the actual wrapper that pins a binding to a specific
// FP-evaluation mode is forge-emitted (V-089/090/091/092/093).  The
// sole reason this namespace exists is so fixy/Default.h can give the
// FpMode axis a `type` alias that satisfies the `every_axis_resolves`
// reflection-driven coverage check.
//
// `Unconstrained` IS the strict default — the binding makes no claim
// about floating-point evaluation policy.  A binding that wants to
// enforce a specific FP mode wraps the relevant value in a forge-
// emitted FpMode wrapper at the call site; the fixy::fn engagement
// marker for this axis is therefore `accept_default_strict_for<
// DimensionAxis::FpMode>` — i.e., "I'm not claiming any wrapper here;
// the wrapper, if any, lives on the value itself."
namespace fp_mode {
    struct Unconstrained {};   // default — no FP-mode wrapper claim at binding scope
}

// ── Dim 23 SyscallSurface (Tier-S, FIXY-V-097, 2026-05-22) ────────
//
// Like Synchronization / Regime / FpMode (dims 20 / 21 / 22),
// SyscallSurface ships NO Fn<...> aggregator slot — the syscall-family
// pin lives at the VALUE site (V-098 ships per-family / per-syscall
// grant tags; V-100 will ship a forge-emitted wrapper that pins the
// concrete SyscallFamily on a return type).  The sole reason this
// namespace exists is so fixy/Default.h can give the SyscallSurface
// axis a `type` alias that satisfies the `every_axis_resolves`
// reflection-driven coverage check.
//
// `Unconstrained` IS the strict default — the binding makes no claim
// about syscall surface.  A binding that wants to constrain the
// surface uses `grant::accept_default_strict_for<DimensionAxis::
// SyscallSurface>` to engage the axis (V-098+ will ship the
// per-family / per-syscall grants that engage with a non-Unconstrained
// claim).
namespace syscall {
    struct Unconstrained {};   // default — no syscall-surface wrapper claim at binding scope
}

// ── Dims 24-28 ControlFlow / CallShape / StackUse / GlobalState /
//    Stdio (Tier-S, FIXY-V-238, 2026-05-23) ───────────────────────────
//
// Five wrapper-only axes sharing the Synchronization / Regime / FpMode /
// SyscallSurface pattern (dims 20-23): NO Fn<...> aggregator slot — the
// discipline lives at the VALUE site (V-239/240/241 ship the lattices,
// V-242 the safety::* Graded wrappers, V-244/245/246 the grant tags).
// Each namespace exists solely so fixy/Default.h can give the axis a
// `type` alias satisfying the `every_axis_resolves` reflection-driven
// coverage check.  `Unconstrained` IS the strict default — the binding
// makes no claim; the wrapper, if any, lives on the value itself.
//
//   ControlFlow  — Pure / AbortOnly / ThrowOnly / MayLongjmp / MaySignal
//   CallShape    — Direct / BoundedRecurses / Indirect / Virtual / Unbounded
//   StackUse     — bounded stack-frame depth discipline
//   GlobalState  — none / readonly / thread-local / mutable-global
//   Stdio        — none / reads / writes on the C stdio surface
namespace control_flow {
    struct Unconstrained {};   // default — no control-flow wrapper claim at binding scope
}
namespace call_shape {
    struct Unconstrained {};   // default — no call-shape wrapper claim at binding scope
}
namespace stack_use {
    struct Unconstrained {};   // default — no stack-use wrapper claim at binding scope
}
namespace global_state {
    struct Unconstrained {};   // default — no global-state wrapper claim at binding scope
}
namespace stdio {
    struct Unconstrained {};   // default — no stdio wrapper claim at binding scope
}

// ── Dims 29-31 HwInstruction / BarrierStrength / SimdIsa
//    (FIXY-V-253, 2026-05-23) ─────────────────────────────────────────
//
// Three wrapper-only HW axes sharing the Synchronization / Regime /
// FpMode / SyscallSurface / V-238 pattern: NO Fn<...> aggregator slot —
// the discipline lives at the VALUE site (V-250/251/252 ship the
// lattices; V-254/255/256 ship the safety::* Graded wrappers; V-257..259
// the grant tags).  Each namespace exists solely so fixy/Default.h can
// give the axis a `type` alias satisfying the `every_axis_resolves`
// reflection-driven coverage check.  `Unconstrained` IS the strict
// default — the binding makes no claim; the wrapper, if any, lives on
// the value itself.
//
//   HwInstruction   — NoneAllowed / Scalar / Vectorizable / Tsc / Msr
//   BarrierStrength — None / CompilerBarrier / Acquire / Release / .. / FullFence
//   SimdIsa         — Scalar / Portable / SSE2..AVX512 / NEON..SVE (Tier-L)
namespace hw_instruction {
    struct Unconstrained {};   // default — no hw-instruction wrapper claim at binding scope
}
namespace barrier_strength {
    struct Unconstrained {};   // default — no barrier-strength wrapper claim at binding scope
}
namespace simd_isa {
    struct Unconstrained {};   // default — no SIMD-ISA wrapper claim at binding scope
}

// ── Dim 32 MemoryScope (FIXY-V-266, 2026-05-23) ──────────────────────
//
// Wrapper-only Tier-L axis sharing the SimdIsa pattern: NO Fn<...>
// aggregator slot — the discipline lives at the VALUE site (V-265 ships
// MemoryScopeLattice; V-267 ships safety::ScopedFence; V-269 the grant
// tags).  This namespace exists solely so fixy/Default.h can give the
// axis a `type` alias satisfying `every_axis_resolves`.  `Unconstrained`
// IS the strict default — the binding makes no memory-visibility-scope
// claim; the scoped fence, if any, lives on the value itself.
//
//   MemoryScope — Thread / Warp / Cta / Cluster / Gpu (accel trunk) ×
//                 Inner(ISH) / Outer(OSH) (ARM trunk), joined at
//                 Thread(⊥) / System(⊤) (Tier-L, non-distributive)
namespace memory_scope {
    struct Unconstrained {};   // default — no memory-scope wrapper claim at binding scope
}

// ═════════════════════════════════════════════════════════════════════
// ── ValidComposition concept gate (Phase 0 P0-2) ───────────────────
// ═════════════════════════════════════════════════════════════════════
//
// CollisionRules<F> is forward-declared here and specialized for
// Fn<...> in safety/CollisionCatalog.h after the Fn template body is
// visible.  This keeps the class-body static_assert dependent: direct
// `Fn<Bad...>` instantiations are rejected, while the catalog can read
// Fn's per-axis aliases (`type_t`, `effect_row_t`, `usage_v`, ...).

template <typename F>
struct CollisionRules;

template <typename F>
concept ValidComposition = CollisionRules<F>::valid;

// ═════════════════════════════════════════════════════════════════════
// ── Fn<...> aggregator template ─────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// 19 positional template parameters per fixy.md §24.1; dim 11
// Observability is derived from EffectRow at the consumer site
// (`with CT` regions only enforce it, per §24.1).  See the catalog
// table at the top of this file for parameter slot order.

template <
    typename       Type,
    typename       Refinement   = pred::True,
    UsageMode      Usage        = UsageMode::Linear,
    typename       EffectRow    = effects::Row<>,
    SecLevel       Security     = SecLevel::Classified,
    typename       Protocol     = proto::None,
    typename       Lifetime     = lifetime::Static,
    typename       Source       = source::FromInternal,
    typename       Trust        = trust::Unverified,                 // FIXY-FOUND-034: Biba-safe bottom (was Verified)
    ReprKind       Repr         = ReprKind::Opaque,
    typename       Cost         = cost::Unstated,
    typename       Precision    = precision::Exact,
    typename       Space        = space::Zero,
    OverflowMode   Overflow     = OverflowMode::Trap,
    MutationMode   Mutation     = MutationMode::Immutable,
    ReentrancyMode Reentrancy   = ReentrancyMode::NonReentrant,
    typename       Size         = size_pol::Unstated,
    std::uint32_t  Version      = 1,
    typename       Staleness    = stale::Fresh
>
struct Fn {
    // ── Type constraint (audit round 2 — five gates) ─────────────
    //
    // Fn<Type, ...> demands a complete object type that is also
    // (a) non-cv-qualified — const/volatile silently break copy-
    //     and move-assignment via implicit deletion of the
    //     defaulted assignment ops (Fn<const int>{}.operator=(...)
    //     gets quietly synthesized as deleted), defeating the
    //     wrapper's discipline; and
    // (b) non-array — `std::is_object_v<int[N]>` is true but the
    //     constructor `Fn(Type v)` would silently rebind to a
    //     pointer (array-to-pointer decay), turning an intended
    //     array copy into a pointer alias.
    //
    // Fixy function bindings emit as function POINTERS or callable
    // structs — never as bare function types — so the void / ref /
    // function-type rejection is structural for every Fixy
    // callsite.  The five test_safety_neg fixtures
    // `neg_fn_rejects_{void,reference,const,volatile,array}_type`
    // pin each gate independently.
    //
    // Diagnostic discipline: each static_assert states the
    // ROOT-CAUSE failure (what breaks downstream) and the FIX
    // (which Fn shape or substrate type to use instead) so a
    // reviewer reaches for the right alternative without reading
    // the wrapper's source.
    static_assert(std::is_object_v<Type>,
        "Fn<Type, ...> requires Type to be a complete object type. "
        "Reject: void, reference types, bare function types.  For "
        "Fixy function bindings, use a function pointer or callable "
        "struct, not the bare function type.");
    static_assert(!std::is_const_v<Type>,
        "Fn<const T, ...> is malformed.  Const-qualifying the value "
        "type silently deletes copy- and move-assignment of the "
        "wrapper, breaking move semantics + the universal mint "
        "discipline.  Use Fn<T, ..., MutationMode::Immutable> to "
        "express logical immutability while keeping the wrapper's "
        "value-category discipline intact.");
    static_assert(!std::is_volatile_v<Type>,
        "Fn<volatile T, ...> is malformed.  volatile is a hardware-"
        "memory annotation (memory-mapped I/O, signal-safe storage) "
        "— it is not a property the Fn grade vector models.  Use "
        "std::atomic<T> for concurrent access or annotate the "
        "volatility at the Type definition site.");
    static_assert(!std::is_array_v<Type>,
        "Fn<T[N], ...> is malformed.  C arrays decay to pointers "
        "in function parameters, so the wrapper's `Fn(Type v)` "
        "constructor would silently rebind to a pointer rather "
        "than copying the array.  Use Fn<std::array<T, N>, ...> "
        "for value-semantic fixed arrays or Fn<Borrowed<T, "
        "Source>, ...> for a borrowed view.");

    // ── Per-axis introspection surface (compile-time accessors) ───
    using type_t                                       = Type;
    using refinement_t                                 = Refinement;
    static constexpr UsageMode      usage_v            = Usage;
    using effect_row_t                                 = EffectRow;
    static constexpr SecLevel       security_v         = Security;
    using protocol_t                                   = Protocol;
    using lifetime_t                                   = Lifetime;
    using source_t                                     = Source;
    using trust_t                                      = Trust;
    static constexpr ReprKind       repr_v             = Repr;
    using cost_t                                       = Cost;
    using precision_t                                  = Precision;
    using space_t                                      = Space;
    static constexpr OverflowMode   overflow_v         = Overflow;
    static constexpr MutationMode   mutation_v         = Mutation;
    static constexpr ReentrancyMode reentrancy_v       = Reentrancy;
    using size_t_                                      = Size;
    static constexpr std::uint32_t  version_v          = Version;
    using staleness_t                                  = Staleness;

    // ── Sole runtime member ──────────────────────────────────────
    // The 19-axis grade vector is type-level only — no per-axis
    // member fields, so EBO is automatic via the absence of empty
    // bases.  The sentinel TU pins the sizeof invariant.
    Type value_{};

    // ── Construction ────────────────────────────────────────────
    constexpr Fn() = default;

    explicit constexpr Fn(Type v)
        noexcept(std::is_nothrow_move_constructible_v<Type>)
        : value_{std::move(v)} {}

    // ── Value access (deducing-this for const/non-const) ────────
    template <typename Self>
    [[nodiscard]] constexpr auto&& value(this Self&& self) noexcept {
        return std::forward<Self>(self).value_;
    }

    // ── ValidComposition gate (P0-2 hookup point) ───────────────
    //
    // Routes through the same concept that `mint_fn` uses — but at
    // the class-template body level, AFTER all per-axis static
    // members are declared, so direct construction
    // (`Fn<X, BadGrades...>{}` bypassing the factory) is gated
    // identically to the mint path.  Without this, when
    // CollisionCatalog.h (P0-2) ships, a user could circumvent the
    // 12 §6.8 rules by writing `Fn<X, BadCombo...>` directly.
    //
    // The gate fires with named per-rule diagnostics from
    // CollisionCatalog.h (I002 / L002 / E044 / I003 / M012 / P002 /
    // I004 / N002 / L003 / M011 / S010 / S011).
    static_assert(ValidComposition<Fn>,
        "Fn<...> grade combination violates a §6.8 collision rule. "
        "See safety/CollisionCatalog.h for the specific rejected rule "
        "(I002 / L002 / E044 / I003 / M012 / P002 / I004 / N002 / "
        "L003 / M011 / S010 / S011).");
};

}  // namespace crucible::safety::fn

#define CRUCIBLE_SAFETY_FN_COLLISION_CATALOG_INTEGRATION 1
#include <crucible/safety/CollisionCatalog.h>
#undef CRUCIBLE_SAFETY_FN_COLLISION_CATALOG_INTEGRATION

namespace crucible::safety::fn {

// ═════════════════════════════════════════════════════════════════════
// ── mint_fn — universal mint pattern (CLAUDE.md §XXI) ──────────────
// ═════════════════════════════════════════════════════════════════════
//
// Token-mint flavor: derives Fn<Type, ...> authority from the
// caller-supplied per-axis grade pack (no Ctx parameter, no
// CtxFitsX gate).  The single concept gate is `ValidComposition`;
// the gate fires for the 12 rejected dimension compositions with
// structured diagnostics.
//
// Two overloads:
//   1. Default-axis: `mint_fn(value)` — every axis takes its
//      default grade.  Fast path for the common case.
//   2. Named-axis:   instantiate Fn<...> directly with explicit
//      per-axis grades, then construct from the value.  No
//      separate factory — direct construction IS the mint when
//      the user wants per-axis customization.
//
// The default-axis form satisfies the universal-mint discipline
// (single-concept requires-clause + [[nodiscard]] + constexpr +
// noexcept-when-T-is-noexcept-movable + concrete return type).

template <typename Type>
    requires ValidComposition<Fn<Type>>
[[nodiscard]] constexpr auto mint_fn(Type v)
    noexcept(std::is_nothrow_move_constructible_v<Type>) -> Fn<Type>
{
    return Fn<Type>{std::move(v)};
}

// ═════════════════════════════════════════════════════════════════════
// ── Self-test (compile-time + reflection-driven coverage) ──────────
// ═════════════════════════════════════════════════════════════════════

namespace detail::fn_self_test {

// ── Storage invariant — the load-bearing zero-runtime-cost claim ──
static_assert(sizeof(Fn<int>) == sizeof(int),
    "Fn<int> with all default grades MUST be byte-equivalent to int. "
    "If this fires, a per-axis member field was added that defeats "
    "EBO collapse — revert to type-level grades only.");
static_assert(sizeof(Fn<char>) == sizeof(char),
    "Fn<char> default grades MUST EBO-collapse.");
static_assert(sizeof(Fn<double>) == sizeof(double),
    "Fn<double> default grades MUST EBO-collapse.");
static_assert(sizeof(Fn<int, pred::True, UsageMode::Affine,
                         effects::Row<>, SecLevel::Public>) == sizeof(int),
    "Customizing non-default grades MUST NOT add runtime storage. "
    "The 19-axis grade vector is type-level only.");

// ── Default-grade introspection ───────────────────────────────────
using DefaultFn = Fn<int>;
static_assert(std::is_same_v<DefaultFn::type_t,         int>);
static_assert(std::is_same_v<DefaultFn::refinement_t,   pred::True>);
static_assert(DefaultFn::usage_v                     == UsageMode::Linear);
static_assert(std::is_same_v<DefaultFn::effect_row_t, effects::Row<>>);
static_assert(DefaultFn::security_v                  == SecLevel::Classified);
static_assert(std::is_same_v<DefaultFn::protocol_t,   proto::None>);
static_assert(std::is_same_v<DefaultFn::lifetime_t,   lifetime::Static>);
static_assert(std::is_same_v<DefaultFn::source_t,     source::FromInternal>);
// FIXY-FOUND-034: unannotated default is trust::Unverified (Biba bottom).
// `trust::Verified` is now an EARNED status — code that has discharged a
// proof obligation must engage it explicitly via grant::trust_verified,
// making the verification surface grep-discoverable.
static_assert(std::is_same_v<DefaultFn::trust_t,      trust::Unverified>);
static_assert(DefaultFn::repr_v                      == ReprKind::Opaque);
static_assert(std::is_same_v<DefaultFn::cost_t,       cost::Unstated>);
static_assert(std::is_same_v<DefaultFn::precision_t,  precision::Exact>);
static_assert(std::is_same_v<DefaultFn::space_t,      space::Zero>);
static_assert(DefaultFn::overflow_v                  == OverflowMode::Trap);
static_assert(DefaultFn::mutation_v                  == MutationMode::Immutable);
static_assert(DefaultFn::reentrancy_v                == ReentrancyMode::NonReentrant);
static_assert(std::is_same_v<DefaultFn::size_t_,      size_pol::Unstated>);
static_assert(DefaultFn::version_v                   == 1);
static_assert(std::is_same_v<DefaultFn::staleness_t,  stale::Fresh>);

// ── Custom-grade introspection ────────────────────────────────────
using CustomFn = Fn<float, pred::True, UsageMode::Affine,
                    effects::Row<>, SecLevel::Public,
                    proto::None, lifetime::Static,
                    source::FromUser, trust::Tested,
                    ReprKind::C, cost::Constant,
                    precision::F32, space::Zero,
                    OverflowMode::Saturate, MutationMode::Mutable,
                    ReentrancyMode::Reentrant, size_pol::Unstated,
                    /*Version=*/3, stale::Fresh>;
static_assert(CustomFn::usage_v      == UsageMode::Affine);
static_assert(CustomFn::security_v   == SecLevel::Public);
static_assert(std::is_same_v<CustomFn::source_t, source::FromUser>);
static_assert(std::is_same_v<CustomFn::trust_t,  trust::Tested>);
static_assert(CustomFn::repr_v       == ReprKind::C);
static_assert(CustomFn::overflow_v   == OverflowMode::Saturate);
static_assert(CustomFn::mutation_v   == MutationMode::Mutable);
static_assert(CustomFn::reentrancy_v == ReentrancyMode::Reentrant);
static_assert(CustomFn::version_v    == 3);

// ── ValidComposition default behavior ─────────────────────────────
static_assert(ValidComposition<Fn<int>>,
    "Default Fn<...> grades must satisfy the §6.8 collision catalog.");
static_assert(ValidComposition<CustomFn>);

// ── mint_fn smoke ─────────────────────────────────────────────────
static_assert(mint_fn(42).value_ == 42);
static_assert(std::is_same_v<decltype(mint_fn(42)), Fn<int>>);
static_assert(std::is_same_v<decltype(mint_fn(3.14)), Fn<double>>);

// ── Reflection-driven enum-name coverage ──────────────────────────
//
// Each per-axis enum's enumerator catalog must round-trip via
// reflection — if a maintainer adds a new enumerator and forgets to
// update downstream switch arms (e.g., in CollisionCatalog.h's
// per-axis case branches), this assert catches the drift at the
// addition site, not in a downstream compile error.

[[nodiscard]] consteval std::size_t usage_mode_count() noexcept {
    return std::meta::enumerators_of(^^UsageMode).size();
}
[[nodiscard]] consteval std::size_t sec_level_count() noexcept {
    return std::meta::enumerators_of(^^SecLevel).size();
}
[[nodiscard]] consteval std::size_t repr_kind_count() noexcept {
    return std::meta::enumerators_of(^^ReprKind).size();
}
[[nodiscard]] consteval std::size_t overflow_mode_count() noexcept {
    return std::meta::enumerators_of(^^OverflowMode).size();
}
[[nodiscard]] consteval std::size_t mutation_mode_count() noexcept {
    return std::meta::enumerators_of(^^MutationMode).size();
}
[[nodiscard]] consteval std::size_t reentrancy_mode_count() noexcept {
    return std::meta::enumerators_of(^^ReentrancyMode).size();
}

static_assert(usage_mode_count()      == 6,
    "UsageMode enumerator count drift — every consumer with switch "
    "arms on UsageMode must add the new arm.");
static_assert(sec_level_count()       == 5,
    "SecLevel enumerator count drift.");
static_assert(repr_kind_count()       == 6,
    "ReprKind enumerator count drift.");
static_assert(overflow_mode_count()   == 4,
    "OverflowMode enumerator count drift.");
static_assert(mutation_mode_count()   == 4,
    "MutationMode enumerator count drift.");
static_assert(reentrancy_mode_count() == 3,
    "ReentrancyMode enumerator count drift.");

// ── Tier-classification cross-check ───────────────────────────────
//
// Every dimension that Fn aggregates must classify under one of
// DimensionTraits.h's 5 Tier families.  This assert pins the
// vocabulary from Fn.h to the dispatch from DimensionTraits.h —
// a future change to either side that breaks the linkage gets
// caught at compile time.

static_assert(tier_of_axis(DimensionAxis::Type)           == TierKind::Foundational);
static_assert(tier_of_axis(DimensionAxis::Refinement)     == TierKind::Foundational);
static_assert(tier_of_axis(DimensionAxis::Usage)          == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::Effect)         == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::Security)       == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::Protocol)       == TierKind::Typestate);
static_assert(tier_of_axis(DimensionAxis::Lifetime)       == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::Provenance)     == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::Trust)          == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::Representation) == TierKind::Lattice);
static_assert(tier_of_axis(DimensionAxis::Complexity)     == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::Precision)      == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::Space)          == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::Overflow)       == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::Mutation)       == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::Reentrancy)     == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::Size)           == TierKind::Semiring);
static_assert(tier_of_axis(DimensionAxis::Version)        == TierKind::Versioned);
static_assert(tier_of_axis(DimensionAxis::Staleness)      == TierKind::Semiring);

// ── Construction sanity ───────────────────────────────────────────
//
// Fn<int> is default-constructible (NSDMI on `value_{}` zero-inits
// per InitSafe).  It is NOT trivially default-constructible because
// the NSDMI is itself a non-trivial initializer — that is the
// correct trade: zero-initialize-by-default guarantees no uninit
// reads, at the cost of `is_trivially_default_constructible_v`.
// Move/assign remain nothrow when T's are.
static_assert(std::is_default_constructible_v<Fn<int>>);
static_assert(std::is_nothrow_default_constructible_v<Fn<int>>);
static_assert(std::is_nothrow_move_constructible_v<Fn<int>>);
static_assert(std::is_nothrow_move_assignable_v<Fn<int>>);
static_assert(std::is_nothrow_copy_constructible_v<Fn<int>>);
static_assert(std::is_trivially_destructible_v<Fn<int>>);

// ── Audit-round-2 gates (positive coverage) ──────────────────────
//
// The new !is_const_v / !is_volatile_v / !is_array_v static_asserts
// in the Fn class body must NOT fire on legitimate types.  These
// instantiations exercise representative valid types so a future
// regression (over-broad rejection) is caught here, not at downstream
// call sites.

// Mutable scalars — pass.
static_assert(sizeof(Fn<unsigned int>) == sizeof(unsigned int));
static_assert(sizeof(Fn<long long>)    == sizeof(long long));

// Aggregates carrying const/array MEMBERS (vs. const/array Type) — pass.
struct AggregateWithConstMember { const int x = 0; int y = 0; };
struct AggregateWithArrayMember { int xs[4]{}; };
static_assert(sizeof(Fn<AggregateWithConstMember>) == sizeof(AggregateWithConstMember));
static_assert(sizeof(Fn<AggregateWithArrayMember>) == sizeof(AggregateWithArrayMember));

// Pointer to const T — pass (the POINTER is non-const, the pointee is const).
static_assert(sizeof(Fn<const int*>) == sizeof(const int*));

// Reference to T as Source-tagged Borrowed — pass (Borrowed is itself
// an object type).  Sentinel — Borrowed isn't included here directly
// to keep this header lightweight; the principle is that wrapping a
// reference inside another wrapper is allowed because the wrapper IS
// an object.

}  // namespace detail::fn_self_test

}  // namespace crucible::safety::fn

// ═════════════════════════════════════════════════════════════════════
// ── FIXY-V-002: row_hash_contribution<Fn<...>> 19-axis fold ────────
// ═════════════════════════════════════════════════════════════════════
//
// Closes Agent 4's verified federation-cache bug: every `safety::fn::Fn
// <Type, ...non-default-axes...>` instantiation previously routed to
// the primary-template `row_hash_contribution<T>::value == 0` slot,
// collapsing capability-divergent bindings to ONE federation cache
// key (ContentHash, RowHash{0}). The audit at /tmp/audit_test.cpp
// (Agent 4 report §T1-B) verified the collision on the patched GCC
// 16: `safety::fn::Fn<int>` and `safety::fn::Fn<int, pred::True,
// UsageMode::Copy>` produced identical row_hash, so the cross-vendor
// numerics federation contract (§7(b) / FORGE.md §23.2) silently
// fragmented under the same key.
//
// The specialization folds ALL 19 axes through `combine_ids` in a
// pinned declaration-order traversal. The fold is order-sensitive by
// design (combine_ids is Boost-style golden-ratio mixed) — Fn's axes
// are POSITIONAL, so axis-position-vs-axis-value disambiguation is
// guaranteed at the cache layer without needing canonicalization.
//
// Axis-fold protocol:
//   • Type-valued axes (Refinement, Protocol, Lifetime, Source, Trust,
//     Cost, Precision, Space, Size, Staleness): contribute their
//     stable_type_id (FNV-1a fold of the reflection display name,
//     FOUND-E07/E08). Two axes with structurally-identical tag
//     classes but distinct types (e.g. source::FromInternal vs
//     source::FromUser) produce DISTINCT contributions.
//   • EffectRow: routes through the existing
//     `row_hash_contribution<effects::Row<Es...>>` specialization —
//     so the EffectRow's set-semantic permutation invariance and
//     cardinality-seeded fold flows transparently into the Fn-axis
//     hash. `Fn<T, ..., EffectRow=Row<IO,Bg>>` and `Fn<T, ...,
//     EffectRow=Row<Bg,IO>>` hash identically (correct: same set).
//   • Type (the payload, axis 1): contributes its OWN
//     `row_hash_contribution_v<Type>` — so a payload that is itself
//     row-bearing (e.g. wrapper-stacked) flows through. For bare
//     payloads (int, float, struct), Type's contribution is 0 and
//     the per-axis salt dominates.
//   • Enum-valued axes (UsageMode Usage, SecLevel Security, ReprKind
//     Repr, OverflowMode Overflow, MutationMode Mutation,
//     ReentrancyMode Reentrancy): cast to underlying uint64_t. The
//     wrapper-tag salt (WRAPPER_SAFETY_FN_TAG, 0x1D) guards against
//     zero-collision when ALL enum axes happen to be 0 (e.g. the
//     default-constructed Fn<int> with every default).
//   • Integer-valued Version (uint32_t): widens to uint64_t, folds.
//     Two Fns with same axes but Version 1 vs Version 2 produce
//     distinct cache slots — supporting Fn API versioning across
//     federation revisions.
//
// Cache-key separation guarantee:
//   • `safety::fn::Fn<T>` ≠ bare `T` (the WRAPPER_SAFETY_FN_TAG salt
//     ensures non-zero contribution).
//   • `safety::fn::Fn<T, Refinement1>` ≠ `safety::fn::Fn<T, Refinement2>`
//     (stable_type_id distinguishes types).
//   • `safety::fn::Fn<T, ..., Usage1>` ≠ `safety::fn::Fn<T, ..., Usage2>`
//     (underlying enum value participates).
//   • `safety::fn::Fn<T, ..., Row<IO>>` ≠ `safety::fn::Fn<T, ..., Row<Bg>>`
//     (delegates to the Row<Es...> specialization).
//
// Specialization lives here (alongside the class definition) per the
// A1-018 "spec next to declaration" convention adopted for
// effects/Resources.h and effects/Concurrent.h. RowHashFold.h's
// open-extension-point doc-block at line 346 explicitly endorses
// out-of-file specializations in the `crucible::safety::diag`
// namespace following the recursive composition discipline.

#include <crucible/safety/diag/RowHashFold.h>

namespace crucible::safety::diag {

template <
    typename       Type,
    typename       Refinement,
    safety::fn::UsageMode      Usage,
    typename       EffectRow,
    safety::fn::SecLevel       Security,
    typename       Protocol,
    typename       Lifetime,
    typename       Source,
    typename       Trust,
    safety::fn::ReprKind       Repr,
    typename       Cost,
    typename       Precision,
    typename       Space,
    safety::fn::OverflowMode   Overflow,
    safety::fn::MutationMode   Mutation,
    safety::fn::ReentrancyMode Reentrancy,
    typename       Size,
    std::uint32_t  Version,
    typename       Staleness
>
struct row_hash_contribution<safety::fn::Fn<
    Type, Refinement, Usage, EffectRow, Security, Protocol, Lifetime,
    Source, Trust, Repr, Cost, Precision, Space, Overflow, Mutation,
    Reentrancy, Size, Version, Staleness>>
{
    static constexpr std::uint64_t value = []() consteval -> std::uint64_t {
        // Start with the wrapper-tag salt; every subsequent
        // combine_ids preserves the salt's discrimination across
        // axis values via Boost-style mixing.
        std::uint64_t h = detail::WRAPPER_SAFETY_FN_TAG;

        // Payload-side row contribution — payload may itself be a
        // row-bearing wrapper stack (e.g. HotPath<Hot, DetSafe<Pure,
        // Computation<R, int>>>). Bare-T contributes 0 — the salt
        // continues to discriminate via subsequent axes.
        h = detail::combine_ids(h, row_hash_contribution_v<Type>);

        // Type-valued axes — stable_type_id captures the tag class's
        // identity (display name's FNV-1a) which is stable within
        // one build per FOUND-E07's V1 contract.
        h = detail::combine_ids(h, stable_type_id<Refinement>);
        h = detail::combine_ids(h, stable_type_id<Protocol>);
        h = detail::combine_ids(h, stable_type_id<Lifetime>);
        h = detail::combine_ids(h, stable_type_id<Source>);
        h = detail::combine_ids(h, stable_type_id<Trust>);
        h = detail::combine_ids(h, stable_type_id<Cost>);
        h = detail::combine_ids(h, stable_type_id<Precision>);
        h = detail::combine_ids(h, stable_type_id<Space>);
        h = detail::combine_ids(h, stable_type_id<Size>);
        h = detail::combine_ids(h, stable_type_id<Staleness>);

        // EffectRow — delegates to the Row<Es...> specialization for
        // set-semantic permutation-invariant folding.
        h = detail::combine_ids(h, row_hash_contribution_v<EffectRow>);

        // Enum-valued axes — underlying uint8_t widens to uint64_t.
        h = detail::combine_ids(h, static_cast<std::uint64_t>(Usage));
        h = detail::combine_ids(h, static_cast<std::uint64_t>(Security));
        h = detail::combine_ids(h, static_cast<std::uint64_t>(Repr));
        h = detail::combine_ids(h, static_cast<std::uint64_t>(Overflow));
        h = detail::combine_ids(h, static_cast<std::uint64_t>(Mutation));
        h = detail::combine_ids(h, static_cast<std::uint64_t>(Reentrancy));

        // Integer-valued Version — folds last.
        h = detail::combine_ids(h, static_cast<std::uint64_t>(Version));
        return h;
    }();
};

// ─── Self-test block — Agent 4's verified bug closes ──────────────

namespace detail::fn_row_hash_self_test {

using crucible::safety::fn::Fn;
using crucible::safety::fn::UsageMode;
using crucible::safety::fn::SecLevel;
using crucible::safety::fn::ReprKind;
using crucible::safety::fn::OverflowMode;
using crucible::safety::fn::MutationMode;
using crucible::safety::fn::ReentrancyMode;
using crucible::effects::Effect;
using crucible::effects::Row;

// Bare T contributes 0; Fn<T> with the salt does NOT.  This is the
// load-bearing closure for Agent 4 T1-B.
static_assert(row_hash_contribution_v<int>            == 0);
static_assert(row_hash_contribution_v<Fn<int>>        != 0);
static_assert(row_hash_contribution_v<Fn<int>>
           != row_hash_contribution_v<int>);

// Default-axis Fn<T> with two distinct payloads collide on the
// Type-axis stable_type_id — but the WRAPPER_SAFETY_FN_TAG salt
// keeps both off the bare-payload zero slot.
static_assert(row_hash_contribution_v<Fn<int>>    != 0);
static_assert(row_hash_contribution_v<Fn<float>>  != 0);
// The two payloads produce DIFFERENT contributions through Type's
// row_hash (both bare = 0, so the only discriminator is the
// stable_type_id of pred::True which is the same — the actual
// discriminator is `safety_fn_t` identity in the cache lookup tier,
// not the row hash). At the row-hash layer, two default-axis Fns
// over bare payloads with different types hash IDENTICALLY because
// row_hash discriminates ROWS, not payloads. This matches the
// Computation<R, T> design contract (2) "payload-blind for bare T".
static_assert(row_hash_contribution_v<Fn<int>>
           == row_hash_contribution_v<Fn<float>>);

// Usage divergence — Linear vs Copy must hash distinctly. This is
// the principal closure shape for Agent 4 T1-A/B.
static_assert(row_hash_contribution_v<Fn<int>>  // Usage = Linear (default)
           != row_hash_contribution_v<Fn<int, crucible::safety::fn::pred::True,
                                            UsageMode::Copy>>);

// Security divergence — Classified vs Public must hash distinctly.
static_assert(row_hash_contribution_v<Fn<int>>  // Security = Classified (default)
           != row_hash_contribution_v<Fn<int, crucible::safety::fn::pred::True,
                                            UsageMode::Linear,
                                            Row<>,
                                            SecLevel::Public>>);

// EffectRow divergence — Row<> vs Row<Effect::IO> must hash distinctly.
static_assert(row_hash_contribution_v<Fn<int>>  // Row = Row<> (default)
           != row_hash_contribution_v<Fn<int, crucible::safety::fn::pred::True,
                                            UsageMode::Linear,
                                            Row<Effect::IO>>>);

// EffectRow permutation invariance — Row<IO, Bg> ≡ Row<Bg, IO>
// (delegates to the Row<Es...> set-semantic fold).
static_assert(row_hash_contribution_v<Fn<int, crucible::safety::fn::pred::True,
                                          UsageMode::Linear,
                                          Row<Effect::IO, Effect::Bg>>>
           == row_hash_contribution_v<Fn<int, crucible::safety::fn::pred::True,
                                          UsageMode::Linear,
                                          Row<Effect::Bg, Effect::IO>>>);

// Sentinel for the federation cache key — Fn<T>'s row_hash is
// non-sentinel (not the documented sentinel slot value).
static_assert(!row_hash_of_v<Fn<int>>.is_sentinel());

}  // namespace detail::fn_row_hash_self_test

}  // namespace crucible::safety::diag
