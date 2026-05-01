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
#include <crucible/algebra/lattices/AllocClassLattice.h>     // AllocClassTag enum
#include <crucible/algebra/lattices/HotPathLattice.h>        // HotPathTier enum
#include <crucible/algebra/lattices/ResidencyHeatLattice.h>  // ResidencyHeatTag enum
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

    // Aliases of the existing Capabilities.h aggregates, exposed
    // under ctx_cap:: for symmetric `with_cap<ctx_cap::*>()` spelling.
    // The aliases ARE the existing types; consumers may use either
    // form (`ctx_cap::Bg` or top-level `Bg`) interchangeably.
    using Bg   = ::crucible::effects::Bg;
    using Init = ::crucible::effects::Init;
    using Test = ::crucible::effects::Test;
}
// The top-level effects::Bg / effects::Init / effects::Test aggregates
// stay structurally usable as the Cap axis (the ctx_cap aliases above
// don't replace them — they just add a uniform spelling).

// Axis 2 (Numa) — NUMA placement policy -----------------------------

namespace ctx_numa {
    struct Any    {};                          // unbound
    struct Local  {};                          // pinned to current thread's home
    struct Spread {};                          // spread across all nodes

    // Pinned<N>: pinned to a specific NUMA node id.  N must be
    // non-negative — node ids are unsigned in every NUMA topology
    // we support (sysfs / /proc/cpuinfo / numactl), so a negative
    // template parameter is structurally a typo.  Caught at the
    // type level so `ctx_numa::Pinned<-1>` fires at instantiation
    // rather than substituting -1 deep into a NUMA codepath.
    template <int Node> struct Pinned {
        static_assert(Node >= 0,
            "ctx_numa::Pinned<N> requires N >= 0; NUMA node ids are "
            "non-negative in every supported topology.");
    };
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

    // ByteBudget<N>: declares the working-set footprint in bytes.
    // N == 0 is meaningless — a context that claims "0 bytes of
    // workload" is either Unspecified (use the sentinel above) or a
    // typo.  Caught at the type level.
    template <std::size_t Bytes> struct ByteBudget {
        static_assert(Bytes > 0,
            "ctx_workload::ByteBudget<N> requires N > 0; use "
            "ctx_workload::Unspecified when no budget is declared.");
    };

    // ItemBudget<N>: declares the work-item count.  Same N == 0 ban.
    template <std::size_t Items> struct ItemBudget {
        static_assert(Items > 0,
            "ctx_workload::ItemBudget<N> requires N > 0; use "
            "ctx_workload::Unspecified when no budget is declared.");
    };
}

// ── Per-axis concept gates ──────────────────────────────────────────
//
// Each axis ships a recognition trait (`is_*_v`) plus a concept
// (`Is*Type` / `Is*Policy` / `Is*Class` / etc.).  Builder methods on
// ExecCtx gate on these so a typo like `with_cap<int>()` fires a
// clean concept-violation diagnostic at the call site rather than
// substituting nonsense into the resulting type.

// ── Cap recognition ────────────────────────────────────────────────
template <class T> struct is_cap_type                : std::false_type {};
template <>        struct is_cap_type<ctx_cap::Fg>   : std::true_type  {};
template <>        struct is_cap_type<Bg>            : std::true_type  {};
template <>        struct is_cap_type<Init>          : std::true_type  {};
template <>        struct is_cap_type<Test>          : std::true_type  {};
template <class T> inline constexpr bool is_cap_type_v = is_cap_type<T>::value;
template <class T> concept IsCapType = is_cap_type_v<T>;

// ── Numa-policy recognition ────────────────────────────────────────
template <class T> struct is_numa_policy                          : std::false_type {};
template <>        struct is_numa_policy<ctx_numa::Any>           : std::true_type  {};
template <>        struct is_numa_policy<ctx_numa::Local>         : std::true_type  {};
template <>        struct is_numa_policy<ctx_numa::Spread>        : std::true_type  {};
template <int N>   struct is_numa_policy<ctx_numa::Pinned<N>>     : std::true_type  {};
template <class T> inline constexpr bool is_numa_policy_v = is_numa_policy<T>::value;
template <class T> concept IsNumaPolicy = is_numa_policy_v<T>;

// ── Alloc-class recognition ────────────────────────────────────────
template <class T> struct is_alloc_class                          : std::false_type {};
template <>        struct is_alloc_class<ctx_alloc::Unbound>      : std::true_type  {};
template <>        struct is_alloc_class<ctx_alloc::Stack>        : std::true_type  {};
template <>        struct is_alloc_class<ctx_alloc::Arena>        : std::true_type  {};
template <>        struct is_alloc_class<ctx_alloc::Pool>         : std::true_type  {};
template <>        struct is_alloc_class<ctx_alloc::HugePage>     : std::true_type  {};
template <>        struct is_alloc_class<ctx_alloc::Heap>         : std::true_type  {};
template <class T> inline constexpr bool is_alloc_class_v = is_alloc_class<T>::value;
template <class T> concept IsAllocClass = is_alloc_class_v<T>;

// ── Heat / Resid recognition ───────────────────────────────────────
template <class T> struct is_heat_tier                  : std::false_type {};
template <>        struct is_heat_tier<ctx_heat::Cold>  : std::true_type  {};
template <>        struct is_heat_tier<ctx_heat::Warm>  : std::true_type  {};
template <>        struct is_heat_tier<ctx_heat::Hot>   : std::true_type  {};
template <class T> inline constexpr bool is_heat_tier_v = is_heat_tier<T>::value;
template <class T> concept IsHeatTier = is_heat_tier_v<T>;

template <class T> struct is_residency_tier                  : std::false_type {};
template <>        struct is_residency_tier<ctx_resid::DRAM> : std::true_type  {};
template <>        struct is_residency_tier<ctx_resid::L3>   : std::true_type  {};
template <>        struct is_residency_tier<ctx_resid::L2>   : std::true_type  {};
template <>        struct is_residency_tier<ctx_resid::L1>   : std::true_type  {};
template <class T> inline constexpr bool is_residency_tier_v = is_residency_tier<T>::value;
template <class T> concept IsResidencyTier = is_residency_tier_v<T>;

// ── Effect-row recognition ─────────────────────────────────────────
template <class T>           struct is_effect_row                  : std::false_type {};
template <Effect... Es>      struct is_effect_row<Row<Es...>>      : std::true_type  {};
template <class T>           inline constexpr bool is_effect_row_v = is_effect_row<T>::value;
template <class T>           concept IsEffectRow = is_effect_row_v<T>;

// ── Workload-hint recognition ──────────────────────────────────────
template <class T>           struct is_workload_hint                              : std::false_type {};
template <>                  struct is_workload_hint<ctx_workload::Unspecified>   : std::true_type  {};
template <std::size_t N>     struct is_workload_hint<ctx_workload::ByteBudget<N>> : std::true_type  {};
template <std::size_t N>     struct is_workload_hint<ctx_workload::ItemBudget<N>> : std::true_type  {};
template <class T>           inline constexpr bool is_workload_hint_v = is_workload_hint<T>::value;
template <class T>           concept IsWorkloadHint = is_workload_hint_v<T>;

// ── Cap-permitted-row trait ─────────────────────────────────────────
//
// Each cap type publishes the maximum effect row it can authorize.
// ExecCtx's invariant is `Row ⊆ cap_permitted_row<Cap>` — a Fg
// context cannot legally claim a Bg-effect row, etc.  The trait
// drives both the structural static_assert in ExecCtx and the
// builder-method requires-clauses that gate `with_cap<>()` and
// `in_row<>()`.
//
// The Bg / Init / Test contexts grant their own thread effect tag
// (Effect::Bg / Effect::Init / Effect::Test) plus the cap::* tokens
// they aggregate (Alloc + IO + Block for Bg/Test, Alloc + IO for
// Init).  Foreground (ctx_cap::Fg) holds NO cap tokens and therefore
// permits only the empty row.
//
// If the catalog gains a new cap kind, add a trait specialization
// below and update the cap-permitted-row coverage assertion in the
// self-test block.

template <class Cap> struct cap_permitted_row;

template <> struct cap_permitted_row<ctx_cap::Fg> {
    using type = Row<>;
};
template <> struct cap_permitted_row<Bg> {
    using type = Row<Effect::Bg, Effect::Alloc, Effect::IO, Effect::Block>;
};
template <> struct cap_permitted_row<Init> {
    using type = Row<Effect::Init, Effect::Alloc, Effect::IO>;
};
template <> struct cap_permitted_row<Test> {
    using type = Row<Effect::Test, Effect::Alloc, Effect::IO, Effect::Block>;
};

template <class Cap>
using cap_permitted_row_t = typename cap_permitted_row<Cap>::type;

// ── Cross-axis coherence rules ──────────────────────────────────────
//
// The per-axis concept gates above ensure each axis is a recognized
// tag.  These traits go further: they refuse incoherent combinations
// that pass the per-axis check but contradict each other.
//
// Heat × Resid: a context promising hot-tier execution AND DRAM
// residency is a contradiction — DRAM is 200-300 cycles, blowing the
// hot-path budget.  Hot ⇒ {L1, L2}; Warm ⇒ {L1, L2, L3}; Cold ⇒
// any.  Pinned-by-CLAUDE.md §VIII cache hierarchy.
//
// Heat × Alloc: hot path forbids Heap (malloc round-trips are
// 200-2000 ns per CRUCIBLE.md §L3); the optimal hot-path allocators
// are Stack / Arena / Pool / HugePage (deterministic O(1) in the
// best case).  Warm/Cold accept any allocator.
//
// These rules don't apply to the Unbound / DRAM defaults — a
// Heat=Cold context with Resid=DRAM is the canonical "no specific
// promise" shape and stays valid.

template <class Heat, class Resid>
struct heat_resid_coherent : std::true_type {};
// Hot ⇒ Resid ∈ {L1, L2}
template <>
struct heat_resid_coherent<ctx_heat::Hot, ctx_resid::DRAM> : std::false_type {};
template <>
struct heat_resid_coherent<ctx_heat::Hot, ctx_resid::L3>   : std::false_type {};
// Warm ⇒ Resid ∈ {L1, L2, L3}
template <>
struct heat_resid_coherent<ctx_heat::Warm, ctx_resid::DRAM> : std::false_type {};

template <class Heat, class Resid>
inline constexpr bool heat_resid_coherent_v =
    heat_resid_coherent<Heat, Resid>::value;

template <class Heat, class Alloc>
struct heat_alloc_coherent : std::true_type {};
// Hot ⇒ Alloc ≠ Heap
template <>
struct heat_alloc_coherent<ctx_heat::Hot, ctx_alloc::Heap> : std::false_type {};

template <class Heat, class Alloc>
inline constexpr bool heat_alloc_coherent_v =
    heat_alloc_coherent<Heat, Alloc>::value;

// ── ExecCtx<Cap, Numa, Alloc, Heat, Resid, Row, Workload> ───────────

template <class Cap      = ctx_cap::Fg,
          class Numa     = ctx_numa::Any,
          class Alloc    = ctx_alloc::Unbound,
          class Heat     = ctx_heat::Cold,
          class Resid    = ctx_resid::DRAM,
          class Row      = ::crucible::effects::Row<>,
          class Workload = ctx_workload::Unspecified>
struct [[nodiscard]] ExecCtx {
    // ── Soundness invariants enforced at the class template ────────
    //
    // Each axis is recognized by its corresponding concept; passing
    // a non-axis type at any position fails substitution here with
    // a clean diagnostic that names the offending axis.  The
    // cap-permitted-row check forbids incoherent combinations like
    // `ExecCtx<ctx_cap::Fg, …, Row<Effect::Bg>>` — a foreground
    // context cannot legally claim a Bg-effect row.
    static_assert(is_cap_type_v<Cap>,
        "ExecCtx Cap parameter must be one of ctx_cap::Fg / Bg / "
        "Init / Test (or their effects:: aggregates); see "
        "is_cap_type for the recognition trait");
    static_assert(is_numa_policy_v<Numa>,
        "ExecCtx Numa parameter must be one of ctx_numa::Any / "
        "Local / Spread / Pinned<N>");
    static_assert(is_alloc_class_v<Alloc>,
        "ExecCtx Alloc parameter must be one of ctx_alloc::Unbound "
        "/ Stack / Arena / Pool / HugePage / Heap");
    static_assert(is_heat_tier_v<Heat>,
        "ExecCtx Heat parameter must be one of ctx_heat::Cold / "
        "Warm / Hot");
    static_assert(is_residency_tier_v<Resid>,
        "ExecCtx Resid parameter must be one of ctx_resid::DRAM / "
        "L3 / L2 / L1");
    static_assert(is_effect_row_v<Row>,
        "ExecCtx Row parameter must be an effects::Row<Es...> "
        "specialization");
    static_assert(is_workload_hint_v<Workload>,
        "ExecCtx Workload parameter must be one of "
        "ctx_workload::Unspecified / ByteBudget<N> / ItemBudget<N>");
    static_assert(Subrow<Row, cap_permitted_row_t<Cap>>,
        "ExecCtx Row must be a Subrow of Cap's permitted row.  "
        "Foreground (ctx_cap::Fg) permits only Row<>; Bg permits "
        "{Bg, Alloc, IO, Block}; Init permits {Init, Alloc, IO}; "
        "Test permits {Test, Alloc, IO, Block}.  See cap_permitted_row.");

    // ── Cross-axis coherence ────────────────────────────────────────
    //
    // Heat × Resid: hot-path data must live in L1/L2; warm-path data
    // must live in L1/L2/L3; cold-path data may live anywhere.  A
    // hot-path-tier promise paired with a DRAM-residency promise is
    // a contradiction the type system catches at instantiation.
    static_assert(heat_resid_coherent_v<Heat, Resid>,
        "ExecCtx Heat × Resid incoherent: Heat=Hot requires Resid ∈ "
        "{L1, L2}; Heat=Warm requires Resid ∈ {L1, L2, L3}; only "
        "Heat=Cold may target DRAM.  See heat_resid_coherent.");

    // Heat × Alloc: hot path forbids Heap (malloc round-trips defeat
    // the tier promise).  Stack / Arena / Pool / HugePage / Unbound
    // are all admissible for Heat=Hot; Warm/Cold accept Heap.
    static_assert(heat_alloc_coherent_v<Heat, Alloc>,
        "ExecCtx Heat × Alloc incoherent: Heat=Hot forbids "
        "Alloc=Heap (200-2000 ns malloc round-trips break the "
        "hot-path budget).  Use Stack / Arena / Pool / HugePage.  "
        "See heat_alloc_coherent.");

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

    // Each builder method gates its NTTP on the matching axis concept
    // AND propagates the cap-permitted-row invariant.  A call site
    // that supplies a non-axis type fires the concept rather than
    // substituting nonsense into the resulting ExecCtx.

    template <class NewCap>
        requires IsCapType<NewCap>
              && Subrow<Row, cap_permitted_row_t<NewCap>>
    [[nodiscard]] consteval auto with_cap() const noexcept
        -> ExecCtx<NewCap, Numa, Alloc, Heat, Resid, Row, Workload> { return {}; }

    template <class NewNuma>
        requires IsNumaPolicy<NewNuma>
    [[nodiscard]] consteval auto pinned_to() const noexcept
        -> ExecCtx<Cap, NewNuma, Alloc, Heat, Resid, Row, Workload> { return {}; }

    template <class NewAlloc>
        requires IsAllocClass<NewAlloc>
    [[nodiscard]] consteval auto with_alloc() const noexcept
        -> ExecCtx<Cap, Numa, NewAlloc, Heat, Resid, Row, Workload> { return {}; }

    template <class NewHeat>
        requires IsHeatTier<NewHeat>
    [[nodiscard]] consteval auto with_heat() const noexcept
        -> ExecCtx<Cap, Numa, Alloc, NewHeat, Resid, Row, Workload> { return {}; }

    template <class NewResid>
        requires IsResidencyTier<NewResid>
    [[nodiscard]] consteval auto with_residency() const noexcept
        -> ExecCtx<Cap, Numa, Alloc, Heat, NewResid, Row, Workload> { return {}; }

    // Row weakening: caller may ENLARGE the row (allow more effects)
    // by virtue of holding the appropriate cap tokens.  The current
    // row must be a Subrow of the new one — i.e. Row ⊆ NewRow.  This
    // matches the F*-style `weaken<R2>()` direction in
    // effects/Computation.h: a context with row R can be promoted to
    // a richer row R' iff R ⊆ R'.  Additionally, the new row must
    // remain a Subrow of Cap's permitted row — a Fg context can never
    // promote to a Bg-effect row, no matter how it's chained.
    template <class NewRow>
        requires IsEffectRow<NewRow>
              && Subrow<Row,    NewRow>
              && Subrow<NewRow, cap_permitted_row_t<Cap>>
    [[nodiscard]] consteval auto in_row() const noexcept
        -> ExecCtx<Cap, Numa, Alloc, Heat, Resid, NewRow, Workload> { return {}; }

    template <class NewWl>
        requires IsWorkloadHint<NewWl>
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
// Vessel-side dispatch op runs in.  Note the explicit Heat=Hot +
// Resid=L1 — the ExecCtx<> default is Cold+DRAM (the most permissive
// shape); HotFgCtx is the named alias that matches its title.
using HotFgCtx = ExecCtx<
    ctx_cap::Fg,
    ctx_numa::Local,
    ctx_alloc::Stack,
    ctx_heat::Hot,
    ctx_resid::L1,
    Row<>
>;

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

// ── ExecCtx recognition + per-axis extractors ───────────────────────
//
// `IsExecCtx` lets callers constrain template parameters to ExecCtx
// instantiations — the canonical pattern is
//
//   template <IsExecCtx Ctx>
//   void Arena::alloc(Ctx const&, Refined<positive, size_t> n);
//
// `*_of_t` extractors mirror safety/GradedExtract.h: callers reading
// per-axis tags from a Ctx use `cap_type_of_t<Ctx>` rather than
// `typename Ctx::cap_type` (saves the `typename`, parallels every
// other extractor in the project).

template <class T> struct is_exec_ctx : std::false_type {};
template <class Cap, class Numa, class Alloc, class Heat,
          class Resid, class Row, class Workload>
struct is_exec_ctx<ExecCtx<Cap, Numa, Alloc, Heat, Resid, Row, Workload>>
    : std::true_type {};
template <class T> inline constexpr bool is_exec_ctx_v = is_exec_ctx<T>::value;
template <class T> concept IsExecCtx = is_exec_ctx_v<T>;

template <IsExecCtx Ctx> using cap_type_of_t      = typename Ctx::cap_type;
template <IsExecCtx Ctx> using numa_policy_of_t   = typename Ctx::numa_policy;
template <IsExecCtx Ctx> using alloc_class_of_t   = typename Ctx::alloc_class;
template <IsExecCtx Ctx> using hot_path_tier_of_t = typename Ctx::hot_path_tier;
template <IsExecCtx Ctx> using residency_of_t     = typename Ctx::residency;
template <IsExecCtx Ctx> using row_type_of_t      = typename Ctx::row_type;
template <IsExecCtx Ctx> using workload_hint_of_t = typename Ctx::workload_hint;

// Per-axis discrimination concepts — for call sites that want to
// specialise on a particular Cap or NUMA placement.
template <class Ctx, class WantCap>
concept HasCap = IsExecCtx<Ctx>
              && std::is_same_v<cap_type_of_t<Ctx>, WantCap>;

template <class Ctx, class WantNuma>
concept HasNumaPolicy = IsExecCtx<Ctx>
                     && std::is_same_v<numa_policy_of_t<Ctx>, WantNuma>;

// ── Cap discrimination shortcuts ────────────────────────────────────
template <class Ctx> concept IsFgCtx   = HasCap<Ctx, ctx_cap::Fg>;
template <class Ctx> concept IsBgCtx   = HasCap<Ctx, Bg>;
template <class Ctx> concept IsInitCtx = HasCap<Ctx, Init>;
template <class Ctx> concept IsTestCtx = HasCap<Ctx, Test>;

// ── Heat-tier discrimination ────────────────────────────────────────
template <class Ctx>
concept IsHotCtx  = IsExecCtx<Ctx>
                 && std::is_same_v<hot_path_tier_of_t<Ctx>, ctx_heat::Hot>;
template <class Ctx>
concept IsWarmCtx = IsExecCtx<Ctx>
                 && std::is_same_v<hot_path_tier_of_t<Ctx>, ctx_heat::Warm>;
template <class Ctx>
concept IsColdCtx = IsExecCtx<Ctx>
                 && std::is_same_v<hot_path_tier_of_t<Ctx>, ctx_heat::Cold>;

// ── Alloc-class discrimination ──────────────────────────────────────
template <class Ctx>
concept IsArenaCtx    = IsExecCtx<Ctx>
                     && std::is_same_v<alloc_class_of_t<Ctx>, ctx_alloc::Arena>;
template <class Ctx>
concept IsHugePageCtx = IsExecCtx<Ctx>
                     && std::is_same_v<alloc_class_of_t<Ctx>, ctx_alloc::HugePage>;
template <class Ctx>
concept IsHeapCtx     = IsExecCtx<Ctx>
                     && std::is_same_v<alloc_class_of_t<Ctx>, ctx_alloc::Heap>;
template <class Ctx>
concept IsStackCtx    = IsExecCtx<Ctx>
                     && std::is_same_v<alloc_class_of_t<Ctx>, ctx_alloc::Stack>;
template <class Ctx>
concept IsPoolCtx     = IsExecCtx<Ctx>
                     && std::is_same_v<alloc_class_of_t<Ctx>, ctx_alloc::Pool>;

// ── Composition concepts ────────────────────────────────────────────
//
// CtxAdmits<Ctx, R>: a function returning Computation<R, T> may be
// called from a context with Ctx::row_type if R ⊆ Ctx::row_type.
// This is the substitution-principle gate that production call sites
// use when accepting a Ctx and dispatching into a row-typed body.
//
//     template <IsExecCtx Ctx>
//         requires CtxAdmits<Ctx, Row<Effect::Bg, Effect::Alloc>>
//     auto BackgroundThread::build_trace(Ctx const&) -> ...;
//
// IsSubCtx<Child, Parent>: child carries the same Cap as parent, and
// child's row is a subset of parent's row.  Used for fork-join: a
// child task derived from a parent must not enlarge the parent's
// authorized effect set.
//
// SiblingCtx<A, B>: A and B share a Cap.  Used to verify two tasks
// running on the same thread (same Cap) for fork-join safety.
//
// CtxOwnsCapability<Ctx, Cap>: the Effect Cap atom is in Ctx's
// permitted row.  Used by per-cap helpers (e.g., a function that
// performs Alloc must verify the Ctx authorizes Effect::Alloc).

template <class Ctx, class R>
concept CtxAdmits = IsExecCtx<Ctx>
                 && IsEffectRow<R>
                 && Subrow<R, row_type_of_t<Ctx>>;

template <class Child, class Parent>
concept IsSubCtx = IsExecCtx<Child>
                && IsExecCtx<Parent>
                && std::is_same_v<cap_type_of_t<Child>, cap_type_of_t<Parent>>
                && Subrow<row_type_of_t<Child>, row_type_of_t<Parent>>;

template <class A, class B>
concept SiblingCtx = IsExecCtx<A>
                  && IsExecCtx<B>
                  && std::is_same_v<cap_type_of_t<A>, cap_type_of_t<B>>;

template <class Ctx, Effect Cap>
concept CtxOwnsCapability = IsExecCtx<Ctx>
                         && row_contains_v<row_type_of_t<Ctx>, Cap>;

// ── Wrapper-enum bridges ────────────────────────────────────────────
//
// ExecCtx's per-axis tags live in ctx_*::* namespaces; the existing
// safety::HotPath / safety::AllocClass / safety::ResidencyHeat
// wrappers consume enum tags from algebra::lattices::*.  These
// bridge metafunctions project an ExecCtx axis tag onto the matching
// lattice enum value — usable by any consumer that wants to flow
// ExecCtx into the wrapper world without spelling the mapping.
//
// Conventions:
//   • Each bridge maps a CTX tag to a single lattice enum value.
//   • Mappings are 1-1 where possible; ctx_resid → ResidencyHeatTag
//     collapses 4 levels into 3 (L1/L2 are both "Hot" residency at
//     the wrapper level; L3 → Warm; DRAM → Cold).
//   • ctx_alloc::Unbound has NO wrapper mapping; the trait is
//     undefined for it (consumers attempting to bridge an Unbound
//     allocator hit a hard error — correct behaviour, since Unbound
//     means "no policy committed").

template <class HeatTag> struct to_hot_path_tier;
template <> struct to_hot_path_tier<ctx_heat::Hot> {
    static constexpr auto value = ::crucible::algebra::lattices::HotPathTier::Hot;
};
template <> struct to_hot_path_tier<ctx_heat::Warm> {
    static constexpr auto value = ::crucible::algebra::lattices::HotPathTier::Warm;
};
template <> struct to_hot_path_tier<ctx_heat::Cold> {
    static constexpr auto value = ::crucible::algebra::lattices::HotPathTier::Cold;
};
template <class HeatTag>
inline constexpr auto to_hot_path_tier_v = to_hot_path_tier<HeatTag>::value;

template <class AllocTag> struct to_alloc_class_tag;
template <> struct to_alloc_class_tag<ctx_alloc::Stack> {
    static constexpr auto value = ::crucible::algebra::lattices::AllocClassTag::Stack;
};
template <> struct to_alloc_class_tag<ctx_alloc::Arena> {
    static constexpr auto value = ::crucible::algebra::lattices::AllocClassTag::Arena;
};
template <> struct to_alloc_class_tag<ctx_alloc::Pool> {
    static constexpr auto value = ::crucible::algebra::lattices::AllocClassTag::Pool;
};
template <> struct to_alloc_class_tag<ctx_alloc::Heap> {
    static constexpr auto value = ::crucible::algebra::lattices::AllocClassTag::Heap;
};
template <> struct to_alloc_class_tag<ctx_alloc::HugePage> {
    static constexpr auto value = ::crucible::algebra::lattices::AllocClassTag::HugePage;
};
// ctx_alloc::Unbound: no specialization; the bridge is uninhabited.
template <class AllocTag>
inline constexpr auto to_alloc_class_tag_v = to_alloc_class_tag<AllocTag>::value;

template <class ResidTag> struct to_residency_heat_tag;
template <> struct to_residency_heat_tag<ctx_resid::L1> {
    static constexpr auto value = ::crucible::algebra::lattices::ResidencyHeatTag::Hot;
};
template <> struct to_residency_heat_tag<ctx_resid::L2> {
    static constexpr auto value = ::crucible::algebra::lattices::ResidencyHeatTag::Hot;
};
template <> struct to_residency_heat_tag<ctx_resid::L3> {
    static constexpr auto value = ::crucible::algebra::lattices::ResidencyHeatTag::Warm;
};
template <> struct to_residency_heat_tag<ctx_resid::DRAM> {
    static constexpr auto value = ::crucible::algebra::lattices::ResidencyHeatTag::Cold;
};
template <class ResidTag>
inline constexpr auto to_residency_heat_tag_v = to_residency_heat_tag<ResidTag>::value;

// ── Atomic batch-builder ────────────────────────────────────────────
//
// rebuild_to<NewCtx>() — single-step transition from one ExecCtx to
// another.  Necessary because individual builder methods evaluate
// the cross-axis coherence rules (heat × resid, heat × alloc) on
// each intermediate type, so a chain that advances Heat before
// Resid fires the invariant on the intermediate state.
// rebuild_to<NewCtx>() creates the destination type in ONE step,
// bypassing intermediate-state checks.  The destination's own
// invariants still fire when the resulting NewCtx is itself
// instantiated, so soundness is preserved.
//
// Example:
//
//     constexpr auto bg_drain =
//         HotFgCtx{}.rebuild_to<BgDrainCtx>();
//
// is equivalent to `BgDrainCtx{}` but expresses the call site's
// intent that the new context is *derived from* the old one.

// Defined as a free function rather than a member so the source
// type need not be known up front — usable on any ExecCtx via ADL
// or qualified call.  Constraint: NewCtx must be a recognized
// ExecCtx instantiation.
template <class NewCtx, IsExecCtx OldCtx>
    requires IsExecCtx<NewCtx>
[[nodiscard]] consteval NewCtx rebuild_ctx_to(OldCtx const&) noexcept {
    return NewCtx{};
}

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

// ── ExecCtx<> primitive defaults (the ALL-AXES-DEFAULT shape) ───────
//
// This is the shape produced by `ExecCtx<>{}` with no template
// arguments — the most permissive context (Cold/DRAM/Unbound).  The
// defaults pin the "nothing claimed" sentinel for every axis.
static_assert(std::is_same_v<typename ExecCtx<>::cap_type,      ctx_cap::Fg>);
static_assert(std::is_same_v<typename ExecCtx<>::numa_policy,   ctx_numa::Any>);
static_assert(std::is_same_v<typename ExecCtx<>::alloc_class,   ctx_alloc::Unbound>);
static_assert(std::is_same_v<typename ExecCtx<>::hot_path_tier, ctx_heat::Cold>);
static_assert(std::is_same_v<typename ExecCtx<>::residency,     ctx_resid::DRAM>);
static_assert(std::is_same_v<typename ExecCtx<>::row_type,      Row<>>);
static_assert(std::is_same_v<typename ExecCtx<>::workload_hint, ctx_workload::Unspecified>);

// ── HotFgCtx — actually Hot+L1 (matches the alias name) ─────────────
//
// HotFgCtx is the named alias for the foreground vessel hot path.
// Distinct from `ExecCtx<>` which uses the default Cold+DRAM
// sentinel — the alias documents the real-world Vessel-side shape.
static_assert(std::is_same_v<typename HotFgCtx::cap_type,      ctx_cap::Fg>);
static_assert(std::is_same_v<typename HotFgCtx::numa_policy,   ctx_numa::Local>);
static_assert(std::is_same_v<typename HotFgCtx::alloc_class,   ctx_alloc::Stack>);
static_assert(std::is_same_v<typename HotFgCtx::hot_path_tier, ctx_heat::Hot>);
static_assert(std::is_same_v<typename HotFgCtx::residency,     ctx_resid::L1>);
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

// Note: residency must be set BEFORE heat when heat advances —
// `with_heat<Hot>()` would fire heat_resid_coherent_v on the
// intermediate type `(..., Heat=Hot, Resid=DRAM)` if Resid stayed
// at its default.  Set Resid first.
constexpr auto _ctx3 = _ctx2.with_alloc<ctx_alloc::Arena>()
                            .with_residency<ctx_resid::L1>()
                            .with_heat<ctx_heat::Hot>();
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

// ── Concept gates ───────────────────────────────────────────────────
static_assert( is_cap_type_v<ctx_cap::Fg>);
static_assert( is_cap_type_v<Bg>);
static_assert( is_cap_type_v<Init>);
static_assert( is_cap_type_v<Test>);
static_assert(!is_cap_type_v<int>);
static_assert(!is_cap_type_v<void*>);

static_assert( is_numa_policy_v<ctx_numa::Any>);
static_assert( is_numa_policy_v<ctx_numa::Local>);
static_assert( is_numa_policy_v<ctx_numa::Spread>);
static_assert( is_numa_policy_v<ctx_numa::Pinned<3>>);
static_assert( is_numa_policy_v<ctx_numa::Pinned<-1>>);  // negative is structurally OK
static_assert(!is_numa_policy_v<int>);

static_assert( is_alloc_class_v<ctx_alloc::Arena>);
static_assert(!is_alloc_class_v<int>);

static_assert( is_heat_tier_v<ctx_heat::Hot>);
static_assert(!is_heat_tier_v<int>);

static_assert( is_residency_tier_v<ctx_resid::L1>);
static_assert(!is_residency_tier_v<int>);

static_assert( is_effect_row_v<Row<>>);
static_assert( is_effect_row_v<Row<Effect::Bg, Effect::Alloc>>);
static_assert(!is_effect_row_v<int>);

static_assert( is_workload_hint_v<ctx_workload::Unspecified>);
static_assert( is_workload_hint_v<ctx_workload::ByteBudget<4096>>);
static_assert( is_workload_hint_v<ctx_workload::ItemBudget<128>>);
static_assert(!is_workload_hint_v<int>);

// ── Cap-permitted-row coverage ─────────────────────────────────────
//
// Pin every cap's permitted row.  A drift here breaks federation
// expectations of "what a Fg / Bg / Init / Test context can carry".

static_assert(std::is_same_v<cap_permitted_row_t<ctx_cap::Fg>, Row<>>);
static_assert(std::is_same_v<cap_permitted_row_t<Bg>,
                              Row<Effect::Bg, Effect::Alloc, Effect::IO, Effect::Block>>);
static_assert(std::is_same_v<cap_permitted_row_t<Init>,
                              Row<Effect::Init, Effect::Alloc, Effect::IO>>);
static_assert(std::is_same_v<cap_permitted_row_t<Test>,
                              Row<Effect::Test, Effect::Alloc, Effect::IO, Effect::Block>>);

// All canonical aliases satisfy the coherence invariant by
// construction (the static_assert in ExecCtx<...> would fire if not).
// These re-state the invariant for documentation.
static_assert(Subrow<typename HotFgCtx::row_type,
                     cap_permitted_row_t<typename HotFgCtx::cap_type>>);
static_assert(Subrow<typename BgDrainCtx::row_type,
                     cap_permitted_row_t<typename BgDrainCtx::cap_type>>);
static_assert(Subrow<typename BgCompileCtx::row_type,
                     cap_permitted_row_t<typename BgCompileCtx::cap_type>>);
static_assert(Subrow<typename ColdInitCtx::row_type,
                     cap_permitted_row_t<typename ColdInitCtx::cap_type>>);
static_assert(Subrow<typename TestRunnerCtx::row_type,
                     cap_permitted_row_t<typename TestRunnerCtx::cap_type>>);

// ── IsExecCtx + extractors ─────────────────────────────────────────
static_assert( is_exec_ctx_v<HotFgCtx>);
static_assert( is_exec_ctx_v<BgDrainCtx>);
static_assert( is_exec_ctx_v<MaxCtx>);
static_assert(!is_exec_ctx_v<int>);
static_assert(!is_exec_ctx_v<Bg>);  // Bg is a Cap, not a Ctx

static_assert(std::is_same_v<cap_type_of_t<BgDrainCtx>, Bg>);
static_assert(std::is_same_v<numa_policy_of_t<BgDrainCtx>, ctx_numa::Local>);
static_assert(std::is_same_v<alloc_class_of_t<BgDrainCtx>, ctx_alloc::Arena>);
static_assert(std::is_same_v<row_type_of_t<BgDrainCtx>,
                              Row<Effect::Bg, Effect::Alloc>>);

static_assert( HasCap<BgDrainCtx, Bg>);
static_assert(!HasCap<BgDrainCtx, ctx_cap::Fg>);
static_assert( HasNumaPolicy<BgDrainCtx, ctx_numa::Local>);

// ── ctx_cap aliases match top-level types ──────────────────────────
static_assert(std::is_same_v<ctx_cap::Bg,   Bg>);
static_assert(std::is_same_v<ctx_cap::Init, Init>);
static_assert(std::is_same_v<ctx_cap::Test, Test>);

// ── Builder coherence: with_cap rejects ill-typed combinations ─────
//
// HotFgCtx has Row<>; with_cap<Bg> succeeds because Row<> ⊆ Bg's
// permitted row.  BgDrainCtx's Row<Bg, Alloc> survives a with_cap<Bg>
// (Bg-permitted), but a hypothetical demote to Init must first
// narrow the row — direct with_cap<Init> would fail because
// Row<Bg, Alloc> ⊄ Init's permitted row {Init, Alloc, IO}.

constexpr auto _bg_promoted = HotFgCtx{}.with_cap<Bg>();
static_assert(std::is_same_v<typename decltype(_bg_promoted)::cap_type, Bg>);
static_assert(std::is_same_v<typename decltype(_bg_promoted)::row_type, Row<>>);

// `BgDrainCtx{}.with_cap<Init>()` is forbidden (Bg-effect row not
// fitting Init permission) — the requires-clause filters this and
// the call site fails with a clean concept-violation.  This is
// exercised by neg-compile fixture neg_exec_ctx_with_cap_demote_loses_row.

// ── Cross-axis coherence rules ─────────────────────────────────────
//
// Pin every legal and every illegal Heat × Resid combination so a
// future revision of either axis catches drift.

// Hot tier accepts L1 and L2 only.
static_assert( heat_resid_coherent_v<ctx_heat::Hot, ctx_resid::L1>);
static_assert( heat_resid_coherent_v<ctx_heat::Hot, ctx_resid::L2>);
static_assert(!heat_resid_coherent_v<ctx_heat::Hot, ctx_resid::L3>);
static_assert(!heat_resid_coherent_v<ctx_heat::Hot, ctx_resid::DRAM>);

// Warm tier accepts L1/L2/L3.
static_assert( heat_resid_coherent_v<ctx_heat::Warm, ctx_resid::L1>);
static_assert( heat_resid_coherent_v<ctx_heat::Warm, ctx_resid::L2>);
static_assert( heat_resid_coherent_v<ctx_heat::Warm, ctx_resid::L3>);
static_assert(!heat_resid_coherent_v<ctx_heat::Warm, ctx_resid::DRAM>);

// Cold tier accepts any residency.
static_assert( heat_resid_coherent_v<ctx_heat::Cold, ctx_resid::L1>);
static_assert( heat_resid_coherent_v<ctx_heat::Cold, ctx_resid::L2>);
static_assert( heat_resid_coherent_v<ctx_heat::Cold, ctx_resid::L3>);
static_assert( heat_resid_coherent_v<ctx_heat::Cold, ctx_resid::DRAM>);

// Heat × Alloc: only the Hot-Heap pair is forbidden.
static_assert( heat_alloc_coherent_v<ctx_heat::Hot,  ctx_alloc::Stack>);
static_assert( heat_alloc_coherent_v<ctx_heat::Hot,  ctx_alloc::Arena>);
static_assert( heat_alloc_coherent_v<ctx_heat::Hot,  ctx_alloc::Pool>);
static_assert( heat_alloc_coherent_v<ctx_heat::Hot,  ctx_alloc::HugePage>);
static_assert( heat_alloc_coherent_v<ctx_heat::Hot,  ctx_alloc::Unbound>);
static_assert(!heat_alloc_coherent_v<ctx_heat::Hot,  ctx_alloc::Heap>);
static_assert( heat_alloc_coherent_v<ctx_heat::Warm, ctx_alloc::Heap>);
static_assert( heat_alloc_coherent_v<ctx_heat::Cold, ctx_alloc::Heap>);

// Canonical aliases satisfy the cross-axis rules by construction —
// the static_assert blocks inside ExecCtx<...> would fire at the
// alias declaration if not.  These re-state the invariant for
// documentation and catch drift if a canonical alias is later
// rewritten.
static_assert(heat_resid_coherent_v<typename HotFgCtx::hot_path_tier,
                                     typename HotFgCtx::residency>);
static_assert(heat_resid_coherent_v<typename BgDrainCtx::hot_path_tier,
                                     typename BgDrainCtx::residency>);
static_assert(heat_resid_coherent_v<typename BgCompileCtx::hot_path_tier,
                                     typename BgCompileCtx::residency>);
static_assert(heat_resid_coherent_v<typename ColdInitCtx::hot_path_tier,
                                     typename ColdInitCtx::residency>);
static_assert(heat_resid_coherent_v<typename TestRunnerCtx::hot_path_tier,
                                     typename TestRunnerCtx::residency>);
static_assert(heat_resid_coherent_v<typename MaxCtx::hot_path_tier,
                                     typename MaxCtx::residency>);

static_assert(heat_alloc_coherent_v<typename HotFgCtx::hot_path_tier,
                                     typename HotFgCtx::alloc_class>);
static_assert(heat_alloc_coherent_v<typename BgDrainCtx::hot_path_tier,
                                     typename BgDrainCtx::alloc_class>);
static_assert(heat_alloc_coherent_v<typename ColdInitCtx::hot_path_tier,
                                     typename ColdInitCtx::alloc_class>);
static_assert(heat_alloc_coherent_v<typename MaxCtx::hot_path_tier,
                                     typename MaxCtx::alloc_class>);

// ── Discrimination concepts ─────────────────────────────────────────
static_assert( IsHotCtx<HotFgCtx>);
static_assert(!IsHotCtx<BgDrainCtx>);
static_assert(!IsHotCtx<ColdInitCtx>);

static_assert( IsWarmCtx<BgDrainCtx>);
static_assert( IsWarmCtx<BgCompileCtx>);
static_assert(!IsWarmCtx<HotFgCtx>);

static_assert( IsColdCtx<ColdInitCtx>);
static_assert( IsColdCtx<TestRunnerCtx>);
static_assert(!IsColdCtx<HotFgCtx>);

static_assert( IsFgCtx<HotFgCtx>);
static_assert(!IsFgCtx<BgDrainCtx>);

static_assert( IsBgCtx<BgDrainCtx>);
static_assert( IsBgCtx<BgCompileCtx>);
static_assert(!IsBgCtx<HotFgCtx>);

static_assert( IsInitCtx<ColdInitCtx>);
static_assert(!IsInitCtx<BgDrainCtx>);

static_assert( IsTestCtx<TestRunnerCtx>);
static_assert(!IsTestCtx<BgDrainCtx>);

static_assert( IsArenaCtx<BgDrainCtx>);
static_assert( IsArenaCtx<BgCompileCtx>);
static_assert(!IsArenaCtx<HotFgCtx>);
static_assert( IsHugePageCtx<MaxCtx>);
static_assert( IsHeapCtx<ColdInitCtx>);
static_assert( IsHeapCtx<TestRunnerCtx>);
static_assert( IsStackCtx<HotFgCtx>);

// ── Composition concepts ────────────────────────────────────────────

// CtxAdmits — Ctx absorbs a row R iff R ⊆ Ctx::row_type.
static_assert( CtxAdmits<HotFgCtx, Row<>>);                        // empty row always admitted
static_assert(!CtxAdmits<HotFgCtx, Row<Effect::Bg>>);              // Fg cannot admit Bg
static_assert( CtxAdmits<BgDrainCtx, Row<Effect::Bg>>);            // Bg admits Bg
static_assert( CtxAdmits<BgDrainCtx, Row<Effect::Bg, Effect::Alloc>>);
static_assert(!CtxAdmits<BgDrainCtx, Row<Effect::IO>>);            // BgDrain (Bg+Alloc) cannot admit IO
static_assert( CtxAdmits<BgCompileCtx, Row<Effect::IO>>);          // BgCompile carries IO
static_assert( CtxAdmits<TestRunnerCtx, Row<Effect::Block>>);

// IsSubCtx — Child has same Cap and a Row that's a subset of Parent's.
static_assert( IsSubCtx<BgDrainCtx,    BgCompileCtx>);             // Row<Bg, Alloc> ⊆ Row<Bg, Alloc, IO>
static_assert(!IsSubCtx<BgCompileCtx,  BgDrainCtx>);               // reverse: superset, not subset
static_assert(!IsSubCtx<HotFgCtx,      BgDrainCtx>);               // different cap
static_assert( IsSubCtx<HotFgCtx,      HotFgCtx>);                 // reflexive

// SiblingCtx — same Cap.
static_assert( SiblingCtx<BgDrainCtx,  BgCompileCtx>);             // both Bg
static_assert(!SiblingCtx<HotFgCtx,    BgDrainCtx>);               // Fg vs Bg

// CtxOwnsCapability — atom in row.
static_assert( CtxOwnsCapability<BgDrainCtx,    Effect::Bg>);
static_assert( CtxOwnsCapability<BgDrainCtx,    Effect::Alloc>);
static_assert(!CtxOwnsCapability<BgDrainCtx,    Effect::IO>);
static_assert( CtxOwnsCapability<BgCompileCtx,  Effect::IO>);
static_assert(!CtxOwnsCapability<HotFgCtx,      Effect::Bg>);

// ── Wrapper-enum bridges ────────────────────────────────────────────

namespace lat = ::crucible::algebra::lattices;

static_assert(to_hot_path_tier_v<ctx_heat::Hot>  == lat::HotPathTier::Hot);
static_assert(to_hot_path_tier_v<ctx_heat::Warm> == lat::HotPathTier::Warm);
static_assert(to_hot_path_tier_v<ctx_heat::Cold> == lat::HotPathTier::Cold);

static_assert(to_alloc_class_tag_v<ctx_alloc::Stack>    == lat::AllocClassTag::Stack);
static_assert(to_alloc_class_tag_v<ctx_alloc::Arena>    == lat::AllocClassTag::Arena);
static_assert(to_alloc_class_tag_v<ctx_alloc::Pool>     == lat::AllocClassTag::Pool);
static_assert(to_alloc_class_tag_v<ctx_alloc::Heap>     == lat::AllocClassTag::Heap);
static_assert(to_alloc_class_tag_v<ctx_alloc::HugePage> == lat::AllocClassTag::HugePage);
// ctx_alloc::Unbound deliberately has no bridge — instantiating
// to_alloc_class_tag_v<ctx_alloc::Unbound> would hard-error.

static_assert(to_residency_heat_tag_v<ctx_resid::L1>   == lat::ResidencyHeatTag::Hot);
static_assert(to_residency_heat_tag_v<ctx_resid::L2>   == lat::ResidencyHeatTag::Hot);
static_assert(to_residency_heat_tag_v<ctx_resid::L3>   == lat::ResidencyHeatTag::Warm);
static_assert(to_residency_heat_tag_v<ctx_resid::DRAM> == lat::ResidencyHeatTag::Cold);

// Bridge applied to canonical aliases — flow ExecCtx into the
// wrapper enum world in one consteval lookup per axis.
static_assert(to_hot_path_tier_v<typename HotFgCtx::hot_path_tier>     == lat::HotPathTier::Hot);
static_assert(to_alloc_class_tag_v<typename BgDrainCtx::alloc_class>   == lat::AllocClassTag::Arena);
static_assert(to_residency_heat_tag_v<typename BgDrainCtx::residency>  == lat::ResidencyHeatTag::Hot);
static_assert(to_alloc_class_tag_v<typename MaxCtx::alloc_class>       == lat::AllocClassTag::HugePage);

// ── Atomic batch-builder ────────────────────────────────────────────
//
// rebuild_ctx_to<NewCtx>(old) bypasses the per-link cross-axis
// invariants that would fire on intermediate builder states (e.g.,
// when widening Heat ahead of Resid).  Soundness is preserved
// because the destination type's own static_asserts still fire.

constexpr auto _rebuilt = rebuild_ctx_to<BgDrainCtx>(HotFgCtx{});
static_assert(std::is_same_v<decltype(_rebuilt), const BgDrainCtx>);

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
    // in a runtime variable.  The chain composes through each link.
    // Residency precedes heat by convention — the cross-axis
    // heat_resid_coherent_v invariant fires on intermediate types
    // when heat advances ahead of residency, so the canonical builder
    // order is `with_residency<>()` then `with_heat<>()`.
    constexpr auto build_bg_drain = []() consteval {
        return ExecCtx<>{}
            .with_cap<Bg>()
            .pinned_to<ctx_numa::Local>()
            .with_alloc<ctx_alloc::Arena>()
            .with_residency<ctx_resid::L2>()
            .with_heat<ctx_heat::Warm>()
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
