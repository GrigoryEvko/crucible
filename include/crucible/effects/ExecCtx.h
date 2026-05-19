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

// Axis 8 (Progress) — termination / liveness claim ------------------
//
// Mirrors safety::ProgressClass_v (algebra/lattices/ProgressLattice.h).
// Lattice order: MayDiverge ⊑ Terminating ⊑ Productive ⊑ Bounded.
// MayDiverge is the bottom / "nothing claimed" sentinel; Bounded is
// the top / wall-clock-budgeted promise.
//
// Closes the F* Pure/Tot/Div expressivity gap (fixy-A3-027 and
// CLAUDE.md §XVI F*-style aliases): a context can now carry a
// termination claim alongside its capability + numa + alloc + heat +
// resid + row + workload axes.  Hot tier requires the Ctx PROMISE
// termination (Terminating ⊒); Warm/Cold accept MayDiverge.
//
// We carry the policy as a tag rather than the enum value to keep
// ExecCtx free of its own includes from algebra/lattices/ — every
// axis lives in this file's own ctx_* namespace, bridged onto the
// algebra enum via to_progress_class_v below.

namespace ctx_progress {
    struct MayDiverge  {};
    struct Terminating {};
    struct Productive  {};
    struct Bounded     {};
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

    // ChannelBudget<Bytes, Producers, Consumers, LatestOnly>: declares
    // the communication shape at a channel/endpoint mint boundary.
    // ByteBudget<N> is enough for scheduler cost decisions, but endpoint
    // topology recommendation also needs producer/consumer cardinality
    // and whether one-to-many traffic is latest-value broadcast.
    //
    // Bidirectional fan only — producer side AND consumer side both
    // present.  Producer-only or consumer-only signalling channels
    // (telemetry emitters with no in-band consumer, audit log readers
    // with no in-band producer) use ProducerOnlyChannel /
    // ConsumerOnlyChannel below, so the shape is visible in the type
    // rather than encoded as a misleading "ChannelBudget<B, P, 1>"
    // with a phantom one-consumer placeholder.  See fixy-A3-026.
    template <std::size_t Bytes,
              std::size_t Producers,
              std::size_t Consumers,
              bool LatestOnly = false>
    struct ChannelBudget {
        static_assert(Bytes > 0,
            "ctx_workload::ChannelBudget requires Bytes > 0; use "
            "ctx_workload::Unspecified when no budget is declared.");
        static_assert(Producers > 0,
            "ctx_workload::ChannelBudget requires Producers > 0; use "
            "ctx_workload::ConsumerOnlyChannel for consumer-only "
            "fan shapes (audit log readers, sink-side observers).");
        static_assert(Consumers > 0,
            "ctx_workload::ChannelBudget requires Consumers > 0; use "
            "ctx_workload::ProducerOnlyChannel for producer-only "
            "fan shapes (telemetry emitters, fire-and-forget signals).");

        static constexpr std::size_t bytes = Bytes;
        static constexpr std::size_t producers = Producers;
        static constexpr std::size_t consumers = Consumers;
        static constexpr bool latest_only = LatestOnly;
    };

    // ProducerOnlyChannel<Bytes, Producers, LatestOnly>: declares a
    // signalling channel with N producers and no in-band consumer.
    // Telemetry emitters (perf::Senses → ring-buffer drain), fire-and-
    // forget broadcast (observe metrics announcement), Plumtree fanout
    // legs where the local node is a pure source.  Defaults
    // `LatestOnly = true` because producer-only shapes are almost
    // always overwrite-latest broadcast — if you need queued history,
    // use ChannelBudget with an explicit consumer side.
    //
    // Consumer-cardinality semantics: downstream topology recommender
    // treats consumers = 0 as "no in-band sink"; the substrate cannot
    // pick OneToMany_Latest / OneToOne by reading workload_shape (no
    // consumer count to dispatch against), so endpoint recommendation
    // falls back to substrate_topology_v<Substr>.  The producer count
    // still drives WorkStealing / Fifo / Lifo scheduler-side decisions.
    template <std::size_t Bytes,
              std::size_t Producers,
              bool LatestOnly = true>
    struct ProducerOnlyChannel {
        static_assert(Bytes > 0,
            "ctx_workload::ProducerOnlyChannel requires Bytes > 0; "
            "use ctx_workload::Unspecified when no budget is declared.");
        static_assert(Producers > 0,
            "ctx_workload::ProducerOnlyChannel requires Producers > 0.");

        static constexpr std::size_t bytes = Bytes;
        static constexpr std::size_t producers = Producers;
        static constexpr std::size_t consumers = 0;
        static constexpr bool latest_only = LatestOnly;
    };

    // ConsumerOnlyChannel<Bytes, Consumers>: declares a signalling
    // channel with N consumers and no in-band producer.  Audit log
    // readers (Cipher event-log scrapers), sink-side observers reading
    // a memory-mapped buffer filled externally, replay-from-disk
    // consumers.  `LatestOnly` is not exposed because consumer-only
    // shapes don't admit a producer that could overwrite — every
    // consumer reads the full historical sequence.
    template <std::size_t Bytes, std::size_t Consumers>
    struct ConsumerOnlyChannel {
        static_assert(Bytes > 0,
            "ctx_workload::ConsumerOnlyChannel requires Bytes > 0; "
            "use ctx_workload::Unspecified when no budget is declared.");
        static_assert(Consumers > 0,
            "ctx_workload::ConsumerOnlyChannel requires Consumers > 0.");

        static constexpr std::size_t bytes = Bytes;
        static constexpr std::size_t producers = 0;
        static constexpr std::size_t consumers = Consumers;
        static constexpr bool latest_only = false;
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
//
// `is_effect_row_v` and `IsEffectRow` strip top-level cv-ref before
// matching so `IsEffectRow<Row<...> const&>` ≡ `IsEffectRow<Row<...>>`.
// This matches the convention established by safety/IsLinear.h,
// IsTagged.h, IsRefined.h et al. — every Is* concept in the project
// behaves uniformly when fed a forwarding-reference deduction.
// fixy-A3-004.
template <class T>           struct is_effect_row                  : std::false_type {};
template <Effect... Es>      struct is_effect_row<Row<Es...>>      : std::true_type  {};
template <class T>           inline constexpr bool is_effect_row_v =
    is_effect_row<std::remove_cvref_t<T>>::value;
template <class T>           concept IsEffectRow = is_effect_row_v<T>;

// ── Workload-hint recognition ──────────────────────────────────────
template <class T>           struct is_workload_hint                              : std::false_type {};
template <>                  struct is_workload_hint<ctx_workload::Unspecified>   : std::true_type  {};
template <std::size_t N>     struct is_workload_hint<ctx_workload::ByteBudget<N>> : std::true_type  {};
template <std::size_t N>     struct is_workload_hint<ctx_workload::ItemBudget<N>> : std::true_type  {};
template <std::size_t Bytes, std::size_t Producers, std::size_t Consumers, bool LatestOnly>
struct is_workload_hint<
    ctx_workload::ChannelBudget<Bytes, Producers, Consumers, LatestOnly>>
    : std::true_type {};
template <std::size_t Bytes, std::size_t Producers, bool LatestOnly>
struct is_workload_hint<
    ctx_workload::ProducerOnlyChannel<Bytes, Producers, LatestOnly>>
    : std::true_type {};
template <std::size_t Bytes, std::size_t Consumers>
struct is_workload_hint<
    ctx_workload::ConsumerOnlyChannel<Bytes, Consumers>>
    : std::true_type {};
template <class T>           inline constexpr bool is_workload_hint_v = is_workload_hint<T>::value;
template <class T>           concept IsWorkloadHint = is_workload_hint_v<T>;

// ── Progress-class recognition ─────────────────────────────────────
template <class T> struct is_progress_class                            : std::false_type {};
template <>        struct is_progress_class<ctx_progress::MayDiverge>  : std::true_type  {};
template <>        struct is_progress_class<ctx_progress::Terminating> : std::true_type  {};
template <>        struct is_progress_class<ctx_progress::Productive>  : std::true_type  {};
template <>        struct is_progress_class<ctx_progress::Bounded>     : std::true_type  {};
template <class T> inline constexpr bool is_progress_class_v = is_progress_class<T>::value;
template <class T> concept IsProgressClass = is_progress_class_v<T>;

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

// Heat × Progress: hot path forbids MayDiverge — a context promising
// hot-tier execution MUST promise termination (a hot-path function
// that may never return blocks the foreground thread and breaks every
// p99 / tail-latency budget downstream).  Terminating / Productive /
// Bounded are all admissible for Heat=Hot; Warm/Cold accept any
// Progress claim (drain loops can plausibly diverge in degenerate
// cases; cold-init paths often have no static termination proof).
// fixy-A3-027.

template <class Heat, class Progress>
struct heat_progress_coherent : std::true_type {};
template <>
struct heat_progress_coherent<ctx_heat::Hot, ctx_progress::MayDiverge>
    : std::false_type {};

template <class Heat, class Progress>
inline constexpr bool heat_progress_coherent_v =
    heat_progress_coherent<Heat, Progress>::value;

// ── WellFormedExecCtxAxes ───────────────────────────────────────────
//
// fixy-A3-020: the seven per-axis recognition traits + the Cap×Row
// Subrow invariant are hoisted from a class-body static_assert
// cascade into a SINGLE concept evaluated by a single static_assert
// at the top of the class body.  Rationale: a typo like
// `ExecCtx<int>{}` used to fire 8 separate "static assertion failed"
// lines from inside the class body, burying the FIRST violation
// under 7 followups.  Folding the seven atoms into ONE concept lets
// concept conjunction short-circuit at the first failing atom — only
// the FIRST violation (e.g. `IsCapType<int>` for `ExecCtx<int>`)
// fires a diagnostic, and the later `Subrow<Row,
// cap_permitted_row_t<Cap>>` atom is NOT substituted, so the
// undeclared `cap_permitted_row<int>::type` access never fires a
// secondary cascade.
//
// Why a body static_assert and NOT a template-head requires-clause:
// the friend declarations of ExecCtx live inside Bg/Init/Test in
// Capabilities.h, and Capabilities.h cannot include EffectRow.h
// (circular).  A requires-clause at the template head would require
// matching constraints on the forward declaration AND the friend
// declarations, but those declarations cannot see Subrow / Row /
// IsEffectRow.  The body static_assert sees the full set, and
// concept short-circuiting still gives the same single-diagnostic
// behaviour as a requires-clause would.
//
// Cross-axis Heat × Resid and Heat × Alloc coherence rules are NOT
// folded into WellFormedExecCtxAxes — they stay as separate
// class-body static_asserts because builder-chain intermediate
// states (rebuild_to<NewCtx>, line ~870) transiently violate them
// between an axis swap and the matching coherence repair.  Pushing
// them in would reject mid-chain reshapes that production code
// depends on.
template <class Cap, class Numa, class Alloc, class Heat,
          class Resid, class Row, class Workload, class Progress>
concept WellFormedExecCtxAxes =
       IsCapType<Cap>
    && IsNumaPolicy<Numa>
    && IsAllocClass<Alloc>
    && IsHeatTier<Heat>
    && IsResidencyTier<Resid>
    && IsEffectRow<Row>
    && IsWorkloadHint<Workload>
    && IsProgressClass<Progress>
    && Subrow<Row, cap_permitted_row_t<Cap>>;

// ── ExecCtx<Cap, Numa, Alloc, Heat, Resid, Row, Workload, Progress> ─

template <class Cap      = ctx_cap::Fg,
          class Numa     = ctx_numa::Any,
          class Alloc    = ctx_alloc::Unbound,
          class Heat     = ctx_heat::Cold,
          class Resid    = ctx_resid::DRAM,
          class Row      = ::crucible::effects::Row<>,
          class Workload = ctx_workload::Unspecified,
          class Progress = ctx_progress::Terminating>
struct [[nodiscard]] ExecCtx {
    // fixy-A3-020 / fixy-A3-027: single concept gate, evaluated FIRST
    // so the diagnostic short-circuits at the first failing axis
    // (e.g.  `IsCapType<int>` for `ExecCtx<int>`).  Replaces the
    // per-axis static_assert cascade that used to live here — each
    // axis line fired independently and a single-axis typo produced
    // 8 unrelated diagnostics.
    static_assert(WellFormedExecCtxAxes<Cap, Numa, Alloc, Heat, Resid, Row, Workload, Progress>,
        "ExecCtx<...> axis well-formedness failed.  One of: Cap is not a "
        "cap type (ctx_cap::Fg / Bg / Init / Test); Numa is not a numa "
        "policy (ctx_numa::Any / Local / Spread / Pinned<N>); Alloc is "
        "not an alloc class (ctx_alloc::Unbound / Stack / Arena / Pool / "
        "HugePage / Heap); Heat is not a heat tier (ctx_heat::Cold / "
        "Warm / Hot); Resid is not a residency tier (ctx_resid::DRAM / "
        "L3 / L2 / L1); Row is not an effect Row<...>; Workload is not "
        "a workload hint (ctx_workload::Unspecified / ByteBudget<N> / "
        "ItemBudget<N> / ChannelBudget<...>); Progress is not a "
        "progress class (ctx_progress::MayDiverge / Terminating / "
        "Productive / Bounded); OR Row ⊄ cap_permitted_row<Cap>.  "
        "See WellFormedExecCtxAxes — fixy-A3-020 / fixy-A3-027.");

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

    // Heat × Progress: hot path forbids MayDiverge.  A hot-path
    // context that may never return blocks the foreground thread and
    // breaks every p99 / tail-latency budget downstream.  Hot ⇒
    // Progress ∈ {Terminating, Productive, Bounded}.  Warm/Cold
    // accept any Progress claim.  fixy-A3-027.
    static_assert(heat_progress_coherent_v<Heat, Progress>,
        "ExecCtx Heat × Progress incoherent: Heat=Hot forbids "
        "Progress=MayDiverge — a hot-path function that may never "
        "return blocks the foreground thread.  Use Progress ∈ "
        "{Terminating, Productive, Bounded}.  See "
        "heat_progress_coherent.");

    [[no_unique_address]] Cap      cap_{};
    [[no_unique_address]] Numa     numa_{};
    [[no_unique_address]] Alloc    alloc_{};
    [[no_unique_address]] Heat     heat_{};
    [[no_unique_address]] Resid    resid_{};
    [[no_unique_address]] Row      row_{};
    [[no_unique_address]] Workload wl_{};
    [[no_unique_address]] Progress progress_{};

    // ── Type-level accessors ───────────────────────────────────────
    //
    // Per-axis aliases exposed to consumers.  A function that wants
    // to specialise on a particular axis writes
    // `requires std::is_same_v<typename Ctx::cap_type, effects::Bg>`
    // or pattern-matches on the alias from inside a partial spec.
    using cap_type       = Cap;
    using numa_policy    = Numa;
    using alloc_class    = Alloc;
    using hot_path_tier  = Heat;
    using residency      = Resid;
    using row_type       = Row;
    using workload_hint  = Workload;
    using progress_class = Progress;

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
        -> ExecCtx<NewCap, Numa, Alloc, Heat, Resid, Row, Workload, Progress> { return {}; }

    template <class NewNuma>
        requires IsNumaPolicy<NewNuma>
    [[nodiscard]] consteval auto pinned_to() const noexcept
        -> ExecCtx<Cap, NewNuma, Alloc, Heat, Resid, Row, Workload, Progress> { return {}; }

    template <class NewAlloc>
        requires IsAllocClass<NewAlloc>
    [[nodiscard]] consteval auto with_alloc() const noexcept
        -> ExecCtx<Cap, Numa, NewAlloc, Heat, Resid, Row, Workload, Progress> { return {}; }

    template <class NewHeat>
        requires IsHeatTier<NewHeat>
    [[nodiscard]] consteval auto with_heat() const noexcept
        -> ExecCtx<Cap, Numa, Alloc, NewHeat, Resid, Row, Workload, Progress> { return {}; }

    template <class NewResid>
        requires IsResidencyTier<NewResid>
    [[nodiscard]] consteval auto with_residency() const noexcept
        -> ExecCtx<Cap, Numa, Alloc, Heat, NewResid, Row, Workload, Progress> { return {}; }

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
        -> ExecCtx<Cap, Numa, Alloc, Heat, Resid, NewRow, Workload, Progress> { return {}; }

    template <class NewWl>
        requires IsWorkloadHint<NewWl>
    [[nodiscard]] consteval auto with_workload() const noexcept
        -> ExecCtx<Cap, Numa, Alloc, Heat, Resid, Row, NewWl, Progress> { return {}; }

    // Progress upgrade: caller may strengthen the termination claim
    // by virtue of having proven a stronger property about the body
    // running in this context.  No subtyping direction enforced at
    // the builder level — the lattice direction is enforced when
    // bridging to safety::Progress<...> (which checks the producer's
    // claim against the consumer's requirement via
    // ProgressLattice::At<>).  fixy-A3-027.
    template <class NewProgress>
        requires IsProgressClass<NewProgress>
    [[nodiscard]] consteval auto with_progress() const noexcept
        -> ExecCtx<Cap, Numa, Alloc, Heat, Resid, Row, Workload, NewProgress> { return {}; }

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
// pure row, no workload budget, terminating.  This is the implicit
// context every Vessel-side dispatch op runs in.  Note the explicit
// Heat=Hot + Resid=L1 + Progress=Terminating — the ExecCtx<> default
// is Cold+DRAM+MayDiverge (the most permissive shape); HotFgCtx is
// the named alias that matches its title.  Hot × MayDiverge would
// fire heat_progress_coherent — Terminating is the cheapest hot-path
// claim that satisfies the coherence rule.
using HotFgCtx = ExecCtx<
    ctx_cap::Fg,
    ctx_numa::Local,
    ctx_alloc::Stack,
    ctx_heat::Hot,
    ctx_resid::L1,
    Row<>,
    ctx_workload::Unspecified,
    ctx_progress::Terminating
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

// `is_exec_ctx_v` and `IsExecCtx` strip top-level cv-ref before
// matching so `IsExecCtx<HotFgCtx const&>` ≡ `IsExecCtx<HotFgCtx>`.
// Mirrors the IsLinear / IsTagged / IsRefined family convention so
// generic code using forwarding-reference deduction does not
// accidentally trip the concept gate.  fixy-A3-004.
template <class T> struct is_exec_ctx : std::false_type {};
template <class Cap, class Numa, class Alloc, class Heat,
          class Resid, class Row, class Workload, class Progress>
struct is_exec_ctx<ExecCtx<Cap, Numa, Alloc, Heat, Resid, Row, Workload, Progress>>
    : std::true_type {};
template <class T> inline constexpr bool is_exec_ctx_v =
    is_exec_ctx<std::remove_cvref_t<T>>::value;
template <class T> concept IsExecCtx = is_exec_ctx_v<T>;

template <IsExecCtx Ctx> using cap_type_of_t       = typename Ctx::cap_type;
template <IsExecCtx Ctx> using numa_policy_of_t    = typename Ctx::numa_policy;
template <IsExecCtx Ctx> using alloc_class_of_t    = typename Ctx::alloc_class;
template <IsExecCtx Ctx> using hot_path_tier_of_t  = typename Ctx::hot_path_tier;
template <IsExecCtx Ctx> using residency_of_t      = typename Ctx::residency;
template <IsExecCtx Ctx> using row_type_of_t       = typename Ctx::row_type;
template <IsExecCtx Ctx> using workload_hint_of_t  = typename Ctx::workload_hint;
template <IsExecCtx Ctx> using progress_class_of_t = typename Ctx::progress_class;

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

// ── Progress-class discrimination ──────────────────────────────────
//
// fixy-A3-027: per-axis specialisation concepts mirror the Heat /
// Alloc family.  Production sites that gate on termination class
// (Forge Phase L row admission, hot-path entry-point invariants,
// warden deadline policy) write `requires IsTerminatingCtx<Ctx>`
// rather than spelling out `std::is_same_v<progress_class_of_t<Ctx>,
// ctx_progress::Terminating>`.

template <class Ctx>
concept IsMayDivergeCtx  = IsExecCtx<Ctx>
                        && std::is_same_v<progress_class_of_t<Ctx>, ctx_progress::MayDiverge>;
template <class Ctx>
concept IsTerminatingCtx = IsExecCtx<Ctx>
                        && std::is_same_v<progress_class_of_t<Ctx>, ctx_progress::Terminating>;
template <class Ctx>
concept IsProductiveCtx  = IsExecCtx<Ctx>
                        && std::is_same_v<progress_class_of_t<Ctx>, ctx_progress::Productive>;
template <class Ctx>
concept IsBoundedCtx     = IsExecCtx<Ctx>
                        && std::is_same_v<progress_class_of_t<Ctx>, ctx_progress::Bounded>;

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

// CtxOwnsAnyOf<Ctx, Es...> / CtxOwnsAllOf<Ctx, Es...> — variadic
// disjunctive / conjunctive lifts of CtxOwnsCapability.  Used by call
// sites whose authorization expression is "one of these effect atoms
// must be present" or "every one of these effect atoms must be
// present".  Both reduce to a single row_contains_v fold; cost is
// identical to writing the expansion by hand, naming is the win.
//
// fixy-A5-039 discipline-scaling gap: across ~17 substrate headers
// (cntp/, topology/, warden/, canopy/, perf/) the inline-expansion
// form
//
//     requires effects::IsExecCtx<Ctx>
//           && (effects::row_contains_v<effects::row_type_of_t<Ctx>,
//                                       effects::Effect::Init>
//               || effects::row_contains_v<effects::row_type_of_t<Ctx>,
//                                          effects::Effect::Bg>)
//
// appears 34+ times.  The named form
//
//     requires effects::CtxOwnsAnyOf<Ctx, effects::Effect::Init,
//                                         effects::Effect::Bg>
//
// is grep-discoverable, absorbs renames of row_type_of_t, and lets a
// reviewer recognize the authorization shape at one glance instead of
// parsing four template substitutions.  The empty-pack case is
// admitted as a documented identity:
//   • CtxOwnsAnyOf<Ctx>  ≡ false    (no atom can satisfy the OR)
//   • CtxOwnsAllOf<Ctx>  ≡ true     (vacuously all atoms in {} satisfy)
// Existing inline-expansion sites are queued for the FIXY-U-101 sweep;
// new authorization sites SHOULD use these named concepts on first
// write.
//
// Example — IncastControlRuntime.h::CtxFitsIncastConfigure:
//
//     template <class Ctx>
//     concept CtxFitsIncastConfigure = effects::CtxOwnsAnyOf<
//         Ctx, effects::Effect::Init, effects::Effect::Bg>;
template <class Ctx, Effect... Es>
concept CtxOwnsAnyOf = IsExecCtx<Ctx>
                    && (row_contains_v<row_type_of_t<Ctx>, Es> || ...);

template <class Ctx, Effect... Es>
concept CtxOwnsAllOf = IsExecCtx<Ctx>
                    && (row_contains_v<row_type_of_t<Ctx>, Es> && ...);

// ── fixy-A5-039 self-test ───────────────────────────────────────────
//
// Lock the variadic-lift semantics with positive AND negative
// witnesses against the canonical Ctx aliases.  These static_asserts
// double as HS14 fixture #1 (compile-time) — see test/test_effects.cpp
// for the runtime smoke and grep-discoverability fixture.

// CtxOwnsAnyOf — disjunctive membership.  At least one Effect in Es
// must appear in Ctx::row_type.
static_assert(CtxOwnsAnyOf<BgDrainCtx, Effect::Bg, Effect::IO>,
    "fixy-A5-039: BgDrainCtx::row = Row<Bg, Alloc> — disjunctive lift "
    "must accept the row because Bg is present");
static_assert(CtxOwnsAnyOf<ColdInitCtx, Effect::Bg, Effect::Init>,
    "fixy-A5-039: ColdInitCtx::row = Row<Init, Alloc, IO> — must "
    "accept because Init is present in the disjunction");
static_assert(!CtxOwnsAnyOf<HotFgCtx, Effect::Bg, Effect::IO,
                                       Effect::Init>,
    "fixy-A5-039: HotFgCtx::row = Row<> (empty) — disjunctive lift "
    "must reject because no Effect atom is present");
static_assert(!CtxOwnsAnyOf<BgDrainCtx>,
    "fixy-A5-039: empty-pack disjunction ≡ false — vacuously no atom "
    "can satisfy the OR fold");

// CtxOwnsAllOf — conjunctive membership.  Every Effect in Es must
// appear in Ctx::row_type.
static_assert(CtxOwnsAllOf<BgDrainCtx, Effect::Bg, Effect::Alloc>,
    "fixy-A5-039: BgDrainCtx::row = Row<Bg, Alloc> — conjunctive "
    "lift must accept the row because both atoms are present");
static_assert(!CtxOwnsAllOf<BgDrainCtx, Effect::Bg, Effect::IO>,
    "fixy-A5-039: BgDrainCtx::row = Row<Bg, Alloc> — conjunctive "
    "lift must reject because IO is NOT in the row (would require "
    "in_row<> promotion first)");
static_assert(CtxOwnsAllOf<BgDrainCtx>,
    "fixy-A5-039: empty-pack conjunction ≡ true — vacuous AND over "
    "no atoms; matches std::conjunction_v on empty pack");
static_assert(!CtxOwnsAllOf<HotFgCtx, Effect::Bg>,
    "fixy-A5-039: HotFgCtx::row = Row<> — conjunctive lift must "
    "reject any non-empty pack because no atoms are present");

// Cross-check: CtxOwnsAnyOf with a single atom degenerates to
// CtxOwnsCapability — keeps the single-atom call sites bit-equivalent
// between the two surfaces.
static_assert(CtxOwnsAnyOf<BgDrainCtx, Effect::Bg> ==
              CtxOwnsCapability<BgDrainCtx, Effect::Bg>,
    "fixy-A5-039: single-atom AnyOf must agree with CtxOwnsCapability");
static_assert(CtxOwnsAllOf<BgDrainCtx, Effect::Bg> ==
              CtxOwnsCapability<BgDrainCtx, Effect::Bg>,
    "fixy-A5-039: single-atom AllOf must agree with CtxOwnsCapability");

// CtxCanMint<Ctx, E>: the Cap-source associated with this Ctx is
// authorized to mint Effect E.  Reduces to membership in the Ctx's
// cap-permitted row (NOT the Ctx's row — those are different: row
// is what's claimed in this scope; permitted_row is what could be
// claimed if needed).  Used by mint_from_ctx in Capability.h.
//
// Example: HotFgCtx has cap_type = ctx_cap::Fg whose permitted_row
// is empty, so CtxCanMint<HotFgCtx, anything> is false.  BgDrainCtx
// has cap_type = Bg whose permitted_row = {Bg, Alloc, IO, Block},
// so CtxCanMint<BgDrainCtx, Effect::IO> is true even though IO is
// NOT in BgDrainCtx::row_type (which is Row<Bg, Alloc>).  The Ctx
// could promote IO into its row via in_row<>(); CtxCanMint says
// "the source has authority to do that".
template <class Ctx, Effect E>
concept CtxCanMint = IsExecCtx<Ctx>
                  && row_contains_v<cap_permitted_row_t<cap_type_of_t<Ctx>>, E>;

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
// Compile-time witnesses for the context algebra. Runtime behavior
// checks belong in dedicated tests, not in this production header.
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
    ctx_workload::ByteBudget<2 * 1024 * 1024>,
    ctx_progress::Bounded
>;
static_assert(sizeof(MaxCtx) == 1, "fully-distinguished ExecCtx must still EBO-collapse");

// ── ExecCtx<> primitive defaults (the ALL-AXES-DEFAULT shape) ───────
//
// This is the shape produced by `ExecCtx<>{}` with no template
// arguments — the most permissive resource-axis context
// (Cold/DRAM/Unbound).  Resource axes default to the bottom / "nothing
// claimed" sentinel.  Progress (axis 8) defaults to Terminating rather
// than the lattice bottom (MayDiverge) because C++ function semantics
// already imply termination; MayDiverge is opt-in for explicitly-
// non-terminating code (drain loops, fixed-point iteration).  This
// default also makes Hot tier × default-Progress coherent without
// requiring every Hot-ctx site to thread Progress=Terminating
// explicitly.  fixy-A3-027.
static_assert(std::is_same_v<typename ExecCtx<>::cap_type,       ctx_cap::Fg>);
static_assert(std::is_same_v<typename ExecCtx<>::numa_policy,    ctx_numa::Any>);
static_assert(std::is_same_v<typename ExecCtx<>::alloc_class,    ctx_alloc::Unbound>);
static_assert(std::is_same_v<typename ExecCtx<>::hot_path_tier,  ctx_heat::Cold>);
static_assert(std::is_same_v<typename ExecCtx<>::residency,      ctx_resid::DRAM>);
static_assert(std::is_same_v<typename ExecCtx<>::row_type,       Row<>>);
static_assert(std::is_same_v<typename ExecCtx<>::workload_hint,  ctx_workload::Unspecified>);
static_assert(std::is_same_v<typename ExecCtx<>::progress_class, ctx_progress::Terminating>);

// ── HotFgCtx — actually Hot+L1 (matches the alias name) ─────────────
//
// HotFgCtx is the named alias for the foreground vessel hot path.
// Distinct from `ExecCtx<>` which uses the default Cold+DRAM
// sentinel — the alias documents the real-world Vessel-side shape.
static_assert(std::is_same_v<typename HotFgCtx::cap_type,       ctx_cap::Fg>);
static_assert(std::is_same_v<typename HotFgCtx::numa_policy,    ctx_numa::Local>);
static_assert(std::is_same_v<typename HotFgCtx::alloc_class,    ctx_alloc::Stack>);
static_assert(std::is_same_v<typename HotFgCtx::hot_path_tier,  ctx_heat::Hot>);
static_assert(std::is_same_v<typename HotFgCtx::residency,      ctx_resid::L1>);
static_assert(std::is_same_v<typename HotFgCtx::row_type,       Row<>>);
static_assert(std::is_same_v<typename HotFgCtx::workload_hint,  ctx_workload::Unspecified>);
static_assert(std::is_same_v<typename HotFgCtx::progress_class, ctx_progress::Terminating>);

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

// Note: residency AND progress must be set BEFORE heat when heat
// advances — `with_heat<Hot>()` would fire heat_resid_coherent_v on
// the intermediate type `(..., Heat=Hot, Resid=DRAM)` if Resid stayed
// at its default; symmetrically, it would fire heat_progress_coherent
// on `(..., Heat=Hot, Progress=MayDiverge)` if Progress stayed at its
// default.  Set Resid AND Progress first.  fixy-A3-027.
constexpr auto _ctx3 = _ctx2.with_alloc<ctx_alloc::Arena>()
                            .with_residency<ctx_resid::L1>()
                            .with_progress<ctx_progress::Terminating>()
                            .with_heat<ctx_heat::Hot>();
static_assert(std::is_same_v<typename decltype(_ctx3)::alloc_class,    ctx_alloc::Arena>);
static_assert(std::is_same_v<typename decltype(_ctx3)::hot_path_tier,  ctx_heat::Hot>);
static_assert(std::is_same_v<typename decltype(_ctx3)::residency,      ctx_resid::L1>);
static_assert(std::is_same_v<typename decltype(_ctx3)::progress_class, ctx_progress::Terminating>);

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

// Progress swap from Terminating to Bounded (a stronger claim).
// The builder accepts any Progress tag — directional ordering only
// kicks in when bridging to safety::Progress<...> via the lattice.
constexpr auto _ctx7 = _ctx6.with_progress<ctx_progress::Bounded>();
static_assert(std::is_same_v<typename decltype(_ctx7)::progress_class,
                              ctx_progress::Bounded>);

// Builder chain remains 1 byte at every link.
static_assert(sizeof(_ctx0) == 1);
static_assert(sizeof(_ctx1) == 1);
static_assert(sizeof(_ctx2) == 1);
static_assert(sizeof(_ctx3) == 1);
static_assert(sizeof(_ctx4) == 1);
static_assert(sizeof(_ctx5) == 1);
static_assert(sizeof(_ctx6) == 1);
static_assert(sizeof(_ctx7) == 1);

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

// fixy-A3-004: cv-ref-stripping discipline (mirrors IsLinear family).
// Generic code using forwarding-reference deduction must still match.
static_assert( is_effect_row_v<Row<> const>);
static_assert( is_effect_row_v<Row<> &>);
static_assert( is_effect_row_v<Row<Effect::Bg> const&>);
static_assert( is_effect_row_v<Row<Effect::Bg> &&>);
static_assert(!is_effect_row_v<int const&>);
static_assert( IsEffectRow<Row<Effect::Bg> const&>);
static_assert( IsEffectRow<Row<Effect::Bg>&&>);

static_assert( is_workload_hint_v<ctx_workload::Unspecified>);
static_assert( is_workload_hint_v<ctx_workload::ByteBudget<4096>>);
static_assert( is_workload_hint_v<ctx_workload::ItemBudget<128>>);
static_assert(!is_workload_hint_v<int>);

// fixy-A3-027: Progress-class recognition gates.
static_assert( is_progress_class_v<ctx_progress::MayDiverge>);
static_assert( is_progress_class_v<ctx_progress::Terminating>);
static_assert( is_progress_class_v<ctx_progress::Productive>);
static_assert( is_progress_class_v<ctx_progress::Bounded>);
static_assert(!is_progress_class_v<int>);
static_assert(!is_progress_class_v<void*>);

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

// fixy-A3-004: cv-ref-stripping discipline (mirrors IsLinear family).
// Without this, `template <IsExecCtx Ctx>` rejected forwarding-ref
// deductions of `HotFgCtx const&` / `HotFgCtx&&` — load-bearing
// user-facing footgun in generic code.
static_assert( is_exec_ctx_v<HotFgCtx const>);
static_assert( is_exec_ctx_v<HotFgCtx&>);
static_assert( is_exec_ctx_v<HotFgCtx const&>);
static_assert( is_exec_ctx_v<HotFgCtx&&>);
static_assert( is_exec_ctx_v<BgDrainCtx const&>);
static_assert(!is_exec_ctx_v<int const&>);
static_assert(!is_exec_ctx_v<Bg const&>);
static_assert( IsExecCtx<HotFgCtx const&>);
static_assert( IsExecCtx<HotFgCtx&&>);

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

// Heat × Progress: only the Hot-MayDiverge pair is forbidden.  All
// other combinations are admissible.  fixy-A3-027.
static_assert(!heat_progress_coherent_v<ctx_heat::Hot,  ctx_progress::MayDiverge>);
static_assert( heat_progress_coherent_v<ctx_heat::Hot,  ctx_progress::Terminating>);
static_assert( heat_progress_coherent_v<ctx_heat::Hot,  ctx_progress::Productive>);
static_assert( heat_progress_coherent_v<ctx_heat::Hot,  ctx_progress::Bounded>);
static_assert( heat_progress_coherent_v<ctx_heat::Warm, ctx_progress::MayDiverge>);
static_assert( heat_progress_coherent_v<ctx_heat::Warm, ctx_progress::Terminating>);
static_assert( heat_progress_coherent_v<ctx_heat::Cold, ctx_progress::MayDiverge>);
static_assert( heat_progress_coherent_v<ctx_heat::Cold, ctx_progress::Bounded>);

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

// Heat × Progress coherence re-stated for canonical aliases.
static_assert(heat_progress_coherent_v<typename HotFgCtx::hot_path_tier,
                                        typename HotFgCtx::progress_class>);
static_assert(heat_progress_coherent_v<typename BgDrainCtx::hot_path_tier,
                                        typename BgDrainCtx::progress_class>);
static_assert(heat_progress_coherent_v<typename ColdInitCtx::hot_path_tier,
                                        typename ColdInitCtx::progress_class>);
static_assert(heat_progress_coherent_v<typename MaxCtx::hot_path_tier,
                                        typename MaxCtx::progress_class>);

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

// fixy-A3-027: Progress-class discrimination witnesses.
static_assert( IsTerminatingCtx<HotFgCtx>);           // alias declared Terminating
static_assert(!IsMayDivergeCtx<HotFgCtx>);
static_assert( IsBoundedCtx<MaxCtx>);                 // self-test MaxCtx declared Bounded
static_assert(!IsTerminatingCtx<MaxCtx>);
// Canonical Bg/Cold/Test aliases inherit default Progress=Terminating —
// the F*-common-case + C++-function-semantics default.  MayDiverge is
// opt-in for explicitly-non-terminating code (drain loops, fixed-point
// iteration) via .with_progress<MayDiverge>() on Cold/Bg contexts only
// (Hot × MayDiverge fires the heat_progress_coherent rule).
static_assert( IsTerminatingCtx<BgDrainCtx>);
static_assert( IsTerminatingCtx<BgCompileCtx>);
static_assert( IsTerminatingCtx<ColdInitCtx>);
static_assert( IsTerminatingCtx<TestRunnerCtx>);

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

// CtxCanMint — atom in cap_permitted_row<Ctx::cap_type>.  Distinct
// from CtxOwnsCapability: this checks the SOURCE'S authority, not
// what the Ctx currently claims in its row.  BgDrainCtx::row_type
// is Row<Bg, Alloc> (CURRENT claim), but the Source Bg permits
// {Bg, Alloc, IO, Block} so CtxCanMint admits all four.
static_assert( CtxCanMint<BgDrainCtx,   Effect::Alloc>);
static_assert( CtxCanMint<BgDrainCtx,   Effect::IO>);     // Bg-permitted, not in current row
static_assert( CtxCanMint<BgDrainCtx,   Effect::Block>);
static_assert( CtxCanMint<BgDrainCtx,   Effect::Bg>);
static_assert(!CtxCanMint<BgDrainCtx,   Effect::Init>);   // Bg can't mint Init
static_assert( CtxCanMint<BgCompileCtx, Effect::Block>);
static_assert( CtxCanMint<ColdInitCtx,  Effect::Alloc>);
static_assert( CtxCanMint<ColdInitCtx,  Effect::IO>);
static_assert(!CtxCanMint<ColdInitCtx,  Effect::Block>); // Init can't mint Block
static_assert(!CtxCanMint<HotFgCtx,     Effect::Alloc>); // Fg permits nothing
static_assert(!CtxCanMint<HotFgCtx,     Effect::Bg>);
static_assert( CtxCanMint<TestRunnerCtx, Effect::Block>);

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

// ── Runtime smoke test (fixy-A3-021) ────────────────────────────────
//
// Drive ExecCtx construction + rebuild + axis-accessor surface
// through a runtime path so the inline body type-checks against
// non-constant arguments.  The 7-axis fixy-A3-020 well-formedness
// concept fires in EVERY ExecCtx instantiation — proving the runtime
// construction path stays clean catches the consteval-vs-constexpr
// regression class that pure-static_assert coverage misses.
inline void runtime_smoke_test() {
    // Default-construct each canonical alias at runtime — proves
    // ExecCtx<8-axis...>{} stays trivially-default-constructible
    // even though every axis member uses [[no_unique_address]].
    [[maybe_unused]] HotFgCtx      hot{};
    [[maybe_unused]] BgDrainCtx    bg{};
    [[maybe_unused]] BgCompileCtx  compile{};
    [[maybe_unused]] ColdInitCtx   cold{};
    [[maybe_unused]] TestRunnerCtx test_ctx{};

    // Drive sizeof at the runtime call site — EBO collapse claim
    // holds against the actual built ctx.  The 8th axis (Progress)
    // is an empty tag struct with [[no_unique_address]] — it cannot
    // grow ctx beyond the 1-byte floor.  fixy-A3-027.
    [[maybe_unused]] auto s1 = sizeof(hot);
    [[maybe_unused]] auto s2 = sizeof(bg);

    // Drive rebuild_ctx_to through a runtime-typed source — the
    // batch-builder docstring asserts this path bypasses per-link
    // cross-axis invariants.  The runtime path MUST stay callable;
    // a future refactor making rebuild_ctx_to consteval-only would
    // silently break production call sites.
    auto rebuilt = rebuild_ctx_to<BgDrainCtx>(hot);
    [[maybe_unused]] BgDrainCtx r_copy = rebuilt;

    // fixy-A3-027: drive the Progress axis through a runtime builder
    // chain.  Witnesses (a) `.with_progress<>()` is callable at
    // runtime, not just consteval, and (b) the returned ctx carries
    // the new Progress tag.  Building from `hot` (Heat=Hot,
    // Progress=Terminating) — promote Terminating → Bounded which
    // satisfies heat_progress_coherent_v unconditionally.
    auto hot_bounded = hot.template with_progress<ctx_progress::Bounded>();
    static_assert(std::is_same_v<typename decltype(hot_bounded)::progress_class,
                                  ctx_progress::Bounded>);
    [[maybe_unused]] auto s3 = sizeof(hot_bounded);

    // Drive Progress axis on a Cold ctx where MayDiverge is permitted —
    // proves the axis works in both Hot-constrained and Cold-unconstrained
    // shapes without firing the Heat × Progress coherence rule.
    auto cold_productive = cold.template with_progress<ctx_progress::Productive>();
    static_assert(std::is_same_v<typename decltype(cold_productive)::progress_class,
                                  ctx_progress::Productive>);
    [[maybe_unused]] auto s4 = sizeof(cold_productive);
}

}  // namespace detail::exec_ctx_self_test

}  // namespace crucible::effects
