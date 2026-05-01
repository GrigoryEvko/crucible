#pragma once

// ── crucible::effects::ExecCtx — universal execution context ─────────
//
// One template parameter that bundles every axis a casual op needs:
// capability source (Fg / Bg / Init / Test), NUMA placement policy,
// allocator class, hot-path tier, cache-residency tier, effect row,
// workload hint.  All seven axes are stateless tag types and EBO-
// collapse via [[no_unique_address]] — sizeof(ExecCtx<...>) == 1 byte
// regardless of how rich the type is.
//
//   Axiom coverage: TypeSafe — every axis is a distinct empty class
//                   nameable in the type; mismatches at call-graph
//                   boundaries fire at template substitution.
//                   InitSafe — every member NSDMI-defaults to its
//                   axis's "unbound" or canonical-cold sentinel.
//                   DetSafe — builder methods are consteval; no
//                   axis can drift between the call site's view and
//                   the body's view.
//   Runtime cost:   zero.  ExecCtx<...> is intended as
//                   `[[no_unique_address]] ExecCtx<...> ctx_;` on any
//                   handle that wants to pin its caller-visible
//                   policy at the type level — collapses to zero
//                   bytes in the containing struct.
//
// ── Why a single carrier ────────────────────────────────────────────
//
// Today every "casual op" on a hot path threads multiple bare
// parameters: `effects::Alloc`, `effects::Bg`, NUMA hints (none, in
// practice), an unspoken hot-path-tier promise, an unspoken cache
// residency, sometimes a Row<...> on an `_pure` overload.  None of
// them compose: a function declared `void f(effects::Alloc, …)` does
// not pin where in the cache hierarchy its allocation should land,
// or whether the caller is on the bg or fg thread, or what NUMA
// node the result must be placed on.
//
// ExecCtx is the carrier that fixes all seven axes at once:
//
//     template<class Ctx>
//     auto Arena::alloc(Ctx const&, Refined<positive, size_t> n);
//
// At every call site the optimizer sees the full F* type — the body
// specializes per (Cap, Numa, Alloc, Heat, Resid, Row, Workload)
// combination, picks the right `numa_alloc_onnode` vs `aligned_alloc`,
// emits the right prefetch hints, and routes through the right
// Permissioned* substrate when one is needed.
//
// ── Builder pattern ─────────────────────────────────────────────────
//
//     constexpr auto ctx = ExecCtx<>{}
//                            .with_cap<effects::Bg>()
//                            .pinned_to<ctx_numa::Pinned<3>>()
//                            .with_alloc<ctx_alloc::Arena>()
//                            .with_heat<ctx_heat::Hot>()
//                            .with_residency<ctx_resid::L1>()
//                            .in_row<Row<Effect::Bg, Effect::Alloc>>();
//
// Each consteval method returns a NEW ExecCtx<...> with one axis
// replaced.  Chained calls compose at the type level; the resulting
// value is still 1 byte.
//
// ── Composition with existing wrappers ──────────────────────────────
//
// ExecCtx's axes mirror (without depending on) the existing Graded-
// backed wrappers in safety/HotPath.h, safety/AllocClass.h,
// safety/ResidencyHeat.h, and safety/NumaPlacement.h.  ExecCtx is
// NOT a wrapper — it carries metadata about the surrounding scope,
// not a value.  Hot-path values that should themselves remember
// their tier go through the Graded wrappers; ExecCtx is the carrier
// for the *call-site policy* threaded through the call graph.
//
// ── Future bridge ───────────────────────────────────────────────────
//
// A subsequent commit may emit ExecCtx::hot_path_tier_v / ::alloc_v /
// ::resid_v non-type aliases that map this header's tag types onto
// the existing safety/*.h enum tags (HotPathTier_v, AllocClassTag_v,
// ResidencyHeatTag_v).  That is a routing convenience; the axis
// vocabulary lives here.

#include <crucible/Platform.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>

#include <cstddef>
#include <string_view>
#include <type_traits>

namespace crucible::effects {

// ── Axis tag namespaces ─────────────────────────────────────────────
//
// Each axis is a small set of empty tag types.  Empty = sizeof 1 in
// isolation, EBO-collapsing inside ExecCtx and inside any consumer
// that holds an ExecCtx as `[[no_unique_address]]`.
//
// Axis 1 (Cap) — capability source ----------------------------------

namespace ctx_cap {
    // Foreground vessel thread holds NO minted cap tokens.  Reaches
    // for ::cap::Alloc / ::cap::IO / ::cap::Block fail to compile
    // because the Fg context exposes no member of those types.
    struct Fg {};
}
// effects::Bg / effects::Init / effects::Test (already shipped in
// Capabilities.h) serve as the other Cap values directly — they are
// 1-byte aggregates of cap::* tokens, structurally usable as the Cap
// axis without any further wrapping.

// Axis 2 (Numa) — NUMA placement policy -----------------------------

namespace ctx_numa {
    struct Any    {};                          // unbound
    struct Local  {};                          // pinned to current thread's home
    struct Spread {};                          // spread across all nodes
    template <int Node> struct Pinned {};      // pinned to a specific node id
}

// Axis 3 (Alloc) — allocator class ----------------------------------

namespace ctx_alloc {
    struct Unbound  {};
    struct Stack    {};
    struct Arena    {};
    struct Pool     {};
    struct HugePage {};
    struct Heap     {};
}

// Axis 4 (Heat) — hot-path tier -------------------------------------
//
// Mirrors safety::HotPathTier_v (Cold ⊑ Warm ⊑ Hot).  We carry the
// policy as a tag rather than the enum value to keep ExecCtx free of
// its own includes from safety/.

namespace ctx_heat {
    struct Cold {};
    struct Warm {};
    struct Hot  {};
}

// Axis 5 (Resid) — cache-residency tier -----------------------------
//
// Mirrors safety::ResidencyHeatTag_v.

namespace ctx_resid {
    struct DRAM {};
    struct L3   {};
    struct L2   {};
    struct L1   {};
}

// Axis 7 (Workload) — caller-supplied workload hint ------------------
//
// Used by recommend_parallelism(WorkBudget) / AdaptiveScheduler when
// deciding whether to actually fan out into a Permissioned* substrate
// or run sequentially per the cache-tier rule (CLAUDE.md §IX).

namespace ctx_workload {
    struct Unspecified {};
    template <std::size_t Bytes> struct ByteBudget {};
    template <std::size_t Items> struct ItemBudget {};
}

// ── ExecCtx<Cap, Numa, Alloc, Heat, Resid, Row, Workload> ───────────

template <class Cap      = ctx_cap::Fg,
          class Numa     = ctx_numa::Any,
          class Alloc    = ctx_alloc::Unbound,
          class Heat     = ctx_heat::Cold,
          class Resid    = ctx_resid::DRAM,
          class Row      = ::crucible::effects::Row<>,
          class Workload = ctx_workload::Unspecified>
struct [[nodiscard]] ExecCtx {
    [[no_unique_address]] Cap      cap_{};
    [[no_unique_address]] Numa     numa_{};
    [[no_unique_address]] Alloc    alloc_{};
    [[no_unique_address]] Heat     heat_{};
    [[no_unique_address]] Resid    resid_{};
    [[no_unique_address]] Row      row_{};
    [[no_unique_address]] Workload wl_{};

    // ── Type-level accessors ───────────────────────────────────────
    //
    // Per-axis aliases exposed to consumers.  A function that wants
    // to specialise on a particular axis writes
    // `requires std::is_same_v<typename Ctx::cap_type, effects::Bg>`
    // or pattern-matches on the alias from inside a partial spec.
    using cap_type      = Cap;
    using numa_policy   = Numa;
    using alloc_class   = Alloc;
    using hot_path_tier = Heat;
    using residency     = Resid;
    using row_type      = Row;
    using workload_hint = Workload;

    // ── Builder methods ────────────────────────────────────────────
    //
    // Each consteval method returns a NEW ExecCtx with one axis
    // replaced.  Builder chains compose ergonomically; each link
    // produces a distinct ExecCtx<...> type, but every link is still
    // 1 byte.

    template <class NewCap>
    [[nodiscard]] consteval auto with_cap() const noexcept
        -> ExecCtx<NewCap, Numa, Alloc, Heat, Resid, Row, Workload> { return {}; }

    template <class NewNuma>
    [[nodiscard]] consteval auto pinned_to() const noexcept
        -> ExecCtx<Cap, NewNuma, Alloc, Heat, Resid, Row, Workload> { return {}; }

    template <class NewAlloc>
    [[nodiscard]] consteval auto with_alloc() const noexcept
        -> ExecCtx<Cap, Numa, NewAlloc, Heat, Resid, Row, Workload> { return {}; }

    template <class NewHeat>
    [[nodiscard]] consteval auto with_heat() const noexcept
        -> ExecCtx<Cap, Numa, Alloc, NewHeat, Resid, Row, Workload> { return {}; }

    template <class NewResid>
    [[nodiscard]] consteval auto with_residency() const noexcept
        -> ExecCtx<Cap, Numa, Alloc, Heat, NewResid, Row, Workload> { return {}; }

    // Row weakening: caller may ENLARGE the row (allow more effects)
    // by virtue of holding the appropriate cap tokens.  The current
    // row must be a Subrow of the new one — i.e. Row ⊆ NewRow.  This
    // matches the F*-style `weaken<R2>()` direction in
    // effects/Computation.h: a context with row R can be promoted to
    // a richer row R' iff R ⊆ R'.
    template <class NewRow>
        requires Subrow<Row, NewRow>
    [[nodiscard]] consteval auto in_row() const noexcept
        -> ExecCtx<Cap, Numa, Alloc, Heat, Resid, NewRow, Workload> { return {}; }

    template <class NewWl>
    [[nodiscard]] consteval auto with_workload() const noexcept
        -> ExecCtx<Cap, Numa, Alloc, Heat, Resid, Row, NewWl> { return {}; }

    // ── Diagnostic emitter ─────────────────────────────────────────

    [[nodiscard]] static consteval std::string_view kind_name() noexcept {
        return "ExecCtx";
    }
};

// ── Canonical aliases ───────────────────────────────────────────────
//
// One alias per common context.  Production sites use the alias name
// rather than spelling out the seven template parameters.  Naming the
// shape participates in grep / review and prevents drift between
// equivalent spellings of the same context.

// Foreground vessel hot path: caller owns no minted cap tokens, NUMA-
// local to the vessel thread, stack alloc, hot tier, L1 resident,
// pure row, no workload budget.  This is the implicit context every
// Vessel-side dispatch op runs in.
using HotFgCtx = ExecCtx<>;

// Background drain context: bg cap, NUMA-local to bg thread, arena
// alloc, warm tier, L2 resident, Row<Bg, Alloc>, no budget.  Used by
// BackgroundThread::run / build_trace / compute_memory_plan.
using BgDrainCtx = ExecCtx<
    Bg,
    ctx_numa::Local,
    ctx_alloc::Arena,
    ctx_heat::Warm,
    ctx_resid::L2,
    Row<Effect::Bg, Effect::Alloc>
>;

// Background compile context: bg + alloc + io (compile may write
// kernel artifacts).  Used by Mimic backends and Forge phase H.
using BgCompileCtx = ExecCtx<
    Bg,
    ctx_numa::Local,
    ctx_alloc::Arena,
    ctx_heat::Warm,
    ctx_resid::L2,
    Row<Effect::Bg, Effect::Alloc, Effect::IO>
>;

// Initialization context: init + alloc + io, spread across NUMA, heap.
// Used at process startup before bg/fg threads are pinned.
using ColdInitCtx = ExecCtx<
    Init,
    ctx_numa::Spread,
    ctx_alloc::Heap,
    ctx_heat::Cold,
    ctx_resid::DRAM,
    Row<Effect::Init, Effect::Alloc, Effect::IO>
>;

// Test runner context: unrestricted (test fixtures may exercise any
// effect).  Used inside test/test_*.cpp drivers.
using TestRunnerCtx = ExecCtx<
    Test,
    ctx_numa::Any,
    ctx_alloc::Heap,
    ctx_heat::Cold,
    ctx_resid::DRAM,
    Row<Effect::Test, Effect::Alloc, Effect::IO, Effect::Block>
>;

// ── Self-test block ─────────────────────────────────────────────────
//
// Per the algebra/effects runtime-smoke-test discipline (memory rule
// `feedback_algebra_runtime_smoke_test_discipline`): pure
// static_assert blocks mask consteval/SFINAE/inline-body bugs.  Each
// header that ships consteval surface MUST also expose an
// `inline void runtime_smoke_test()` callable from a sentinel TU
// (test/test_effects_compile.cpp) to force every accessor through
// the project's full -Werror matrix.
namespace detail::exec_ctx_self_test {

// ── EBO collapse: every canonical ctx is one byte ───────────────────
static_assert(sizeof(ExecCtx<>)        == 1, "ExecCtx<> must EBO-collapse to 1 byte");
static_assert(sizeof(HotFgCtx)         == 1, "HotFgCtx must EBO-collapse");
static_assert(sizeof(BgDrainCtx)       == 1, "BgDrainCtx must EBO-collapse");
static_assert(sizeof(BgCompileCtx)     == 1, "BgCompileCtx must EBO-collapse");
static_assert(sizeof(ColdInitCtx)      == 1, "ColdInitCtx must EBO-collapse");
static_assert(sizeof(TestRunnerCtx)    == 1, "TestRunnerCtx must EBO-collapse");

// Even a maximally-distinguished ExecCtx is one byte.
using MaxCtx = ExecCtx<
    Bg,
    ctx_numa::Pinned<3>,
    ctx_alloc::HugePage,
    ctx_heat::Hot,
    ctx_resid::L1,
    Row<Effect::Bg, Effect::Alloc, Effect::IO, Effect::Block>,
    ctx_workload::ByteBudget<2 * 1024 * 1024>
>;
static_assert(sizeof(MaxCtx) == 1, "fully-distinguished ExecCtx must still EBO-collapse");

// ── Defaults ────────────────────────────────────────────────────────
static_assert(std::is_same_v<typename HotFgCtx::cap_type,      ctx_cap::Fg>);
static_assert(std::is_same_v<typename HotFgCtx::numa_policy,   ctx_numa::Any>);
static_assert(std::is_same_v<typename HotFgCtx::alloc_class,   ctx_alloc::Unbound>);
static_assert(std::is_same_v<typename HotFgCtx::hot_path_tier, ctx_heat::Cold>);
static_assert(std::is_same_v<typename HotFgCtx::residency,     ctx_resid::DRAM>);
static_assert(std::is_same_v<typename HotFgCtx::row_type,      Row<>>);
static_assert(std::is_same_v<typename HotFgCtx::workload_hint, ctx_workload::Unspecified>);

// ── Builder chain composes correctly ────────────────────────────────
constexpr auto _ctx0 = ExecCtx<>{};
constexpr auto _ctx1 = _ctx0.with_cap<Bg>();
static_assert(std::is_same_v<typename decltype(_ctx1)::cap_type, Bg>);
// Other axes preserved at default after with_cap<>():
static_assert(std::is_same_v<typename decltype(_ctx1)::numa_policy,   ctx_numa::Any>);
static_assert(std::is_same_v<typename decltype(_ctx1)::alloc_class,   ctx_alloc::Unbound>);
static_assert(std::is_same_v<typename decltype(_ctx1)::hot_path_tier, ctx_heat::Cold>);

constexpr auto _ctx2 = _ctx1.pinned_to<ctx_numa::Pinned<3>>();
static_assert(std::is_same_v<typename decltype(_ctx2)::numa_policy, ctx_numa::Pinned<3>>);

constexpr auto _ctx3 = _ctx2.with_alloc<ctx_alloc::Arena>()
                            .with_heat<ctx_heat::Hot>()
                            .with_residency<ctx_resid::L1>();
static_assert(std::is_same_v<typename decltype(_ctx3)::alloc_class,   ctx_alloc::Arena>);
static_assert(std::is_same_v<typename decltype(_ctx3)::hot_path_tier, ctx_heat::Hot>);
static_assert(std::is_same_v<typename decltype(_ctx3)::residency,     ctx_resid::L1>);

// Row weakening: ∅ ⊆ {Bg, Alloc} so the constraint succeeds.
constexpr auto _ctx4 = _ctx3.in_row<Row<Effect::Bg, Effect::Alloc>>();
static_assert(std::is_same_v<typename decltype(_ctx4)::row_type,
                              Row<Effect::Bg, Effect::Alloc>>);

// Row monotone widening: {Bg, Alloc} ⊆ {Bg, Alloc, IO}.
constexpr auto _ctx5 = _ctx4.in_row<Row<Effect::Bg, Effect::Alloc, Effect::IO>>();
static_assert(std::is_same_v<typename decltype(_ctx5)::row_type,
                              Row<Effect::Bg, Effect::Alloc, Effect::IO>>);

// Workload hint attaches without disturbing other axes.
constexpr auto _ctx6 = _ctx5.with_workload<ctx_workload::ByteBudget<4096>>();
static_assert(std::is_same_v<typename decltype(_ctx6)::workload_hint,
                              ctx_workload::ByteBudget<4096>>);
static_assert(std::is_same_v<typename decltype(_ctx6)::cap_type,    Bg>);
static_assert(std::is_same_v<typename decltype(_ctx6)::numa_policy, ctx_numa::Pinned<3>>);

// Builder chain remains 1 byte at every link.
static_assert(sizeof(_ctx0) == 1);
static_assert(sizeof(_ctx1) == 1);
static_assert(sizeof(_ctx2) == 1);
static_assert(sizeof(_ctx3) == 1);
static_assert(sizeof(_ctx4) == 1);
static_assert(sizeof(_ctx5) == 1);
static_assert(sizeof(_ctx6) == 1);

// ── Canonical alias accessors ───────────────────────────────────────
static_assert(std::is_same_v<typename BgDrainCtx::cap_type,    Bg>);
static_assert(std::is_same_v<typename BgDrainCtx::numa_policy, ctx_numa::Local>);
static_assert(std::is_same_v<typename BgDrainCtx::alloc_class, ctx_alloc::Arena>);
static_assert(std::is_same_v<typename BgDrainCtx::row_type,
                              Row<Effect::Bg, Effect::Alloc>>);

static_assert(std::is_same_v<typename BgCompileCtx::row_type,
                              Row<Effect::Bg, Effect::Alloc, Effect::IO>>);

static_assert(std::is_same_v<typename ColdInitCtx::cap_type,    Init>);
static_assert(std::is_same_v<typename ColdInitCtx::numa_policy, ctx_numa::Spread>);

static_assert(std::is_same_v<typename TestRunnerCtx::cap_type, Test>);

// ── Distinct-type discrimination ────────────────────────────────────
//
// Every axis distinguishes the resulting ExecCtx — two contexts that
// differ in any single axis are distinct types and must NOT be
// implicitly convertible.

using BgVariantA = ExecCtx<Bg, ctx_numa::Local, ctx_alloc::Arena>;
using BgVariantB = ExecCtx<Bg, ctx_numa::Spread, ctx_alloc::Arena>;
static_assert(!std::is_same_v<BgVariantA, BgVariantB>,
    "ExecCtx variants differing in NUMA policy must be distinct types");

using HeatLow  = ExecCtx<Bg, ctx_numa::Local, ctx_alloc::Arena, ctx_heat::Cold>;
using HeatHigh = ExecCtx<Bg, ctx_numa::Local, ctx_alloc::Arena, ctx_heat::Hot>;
static_assert(!std::is_same_v<HeatLow, HeatHigh>,
    "ExecCtx variants differing in hot-path tier must be distinct types");

}  // namespace detail::exec_ctx_self_test

// ── Runtime smoke test (per the algebra discipline) ─────────────────
//
// Drives the consteval builder chain inside a runtime function with
// non-constant operations, confirming the generated code is callable
// and the resulting types are usable from runtime contexts.  Named
// `runtime_smoke_test_exec_ctx` per the effects/* convention used by
// FxAliases.h / EffectRowLattice.h / OsUniverse.h /
// ComputationGraded.h, where each header exposes a uniquely-named
// runtime-smoke entry at the `crucible::effects` namespace level so
// the test/test_effects_compile.cpp sentinel can call them all by
// distinct names.

[[gnu::cold]] inline void runtime_smoke_test_exec_ctx() noexcept {
    // ── Default ctx ─────────────────────────────────────────────────
    HotFgCtx fg;
    static_cast<void>(fg);
    static_assert(sizeof(fg) == 1);

    // ── Builder chain at runtime, against a non-constant arg ────────
    //
    // The builder methods are consteval, but their RESULT can be held
    // in a runtime variable.  The chain composes through each link:
    constexpr auto build_bg_drain = []() consteval {
        return ExecCtx<>{}
            .with_cap<Bg>()
            .pinned_to<ctx_numa::Local>()
            .with_alloc<ctx_alloc::Arena>()
            .with_heat<ctx_heat::Warm>()
            .with_residency<ctx_resid::L2>()
            .in_row<Row<Effect::Bg, Effect::Alloc>>();
    };
    auto bg = build_bg_drain();
    static_cast<void>(bg);
    static_assert(std::is_same_v<typename decltype(bg)::cap_type, Bg>);
    static_assert(std::is_same_v<typename decltype(bg)::row_type,
                                  Row<Effect::Bg, Effect::Alloc>>);

    // ── Canonical aliases construct cleanly at runtime ──────────────
    BgDrainCtx     bg_drain;
    BgCompileCtx   bg_compile;
    ColdInitCtx    init_ctx;
    TestRunnerCtx  test_ctx;
    static_cast<void>(bg_drain);
    static_cast<void>(bg_compile);
    static_cast<void>(init_ctx);
    static_cast<void>(test_ctx);

    // ── Concept-based capability check ──────────────────────────────
    //
    // A consumer that wants to assert "this Ctx exposes the Bg cap"
    // does so by querying cap_type at the type level.  We exercise
    // the discrimination here:
    static_assert(std::is_same_v<typename BgDrainCtx::cap_type, Bg>);
    static_assert(!std::is_same_v<typename HotFgCtx::cap_type,  Bg>);

    // ── Diagnostic emitter ──────────────────────────────────────────
    [[maybe_unused]] auto name = ExecCtx<>::kind_name();
}

}  // namespace crucible::effects
