#pragma once

// One parameter that carries every axis of call-site policy at once:
// which capability source the caller holds, where memory should land,
// which allocator to reach for, how hot the path is, which cache level
// the data should sit in, which effects are claimed, how much work is
// coming, and whether the call is promised to finish.
//
// Threading these as separate bare parameters does not compose.  A
// function taking a bare allocation tag says nothing about the NUMA
// node, the cache level, or the thread it runs on, so the body cannot
// choose between allocators or emit the right prefetch.  With the
// whole policy in one type, the body specializes on all of it.
//
// An ExecCtx describes the surrounding scope, not a value.  A value
// that should remember its own tier carries a wrapper of its own.

#include <crucible/Platform.h>
#include <crucible/algebra/lattices/AllocClassLattice.h>
#include <crucible/algebra/lattices/HotPathLattice.h>
#include <crucible/algebra/lattices/ResidencyHeatLattice.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>

#include <cstddef>
#include <string_view>
#include <type_traits>

namespace crucible::effects {

namespace ctx_cap {
// The foreground thread holds no minted capability token.  A reach for
// one fails to compile, because this context has no member of any
// capability type.
struct Fg {};

// These name the same three types the enclosing namespace declares.
// Either spelling works; the aliases only give the axis a uniform look.
using Bg = ::crucible::effects::Bg;
using Init = ::crucible::effects::Init;
using Test = ::crucible::effects::Test;
}  // namespace ctx_cap

namespace ctx_numa {
struct Any {};  // any node
struct Local {};  // the home node of the running thread
struct Spread {};  // across all nodes

template <int Node>
struct Pinned {
    static_assert(Node >= 0, "ctx_numa::Pinned<N> requires N >= 0.  Node ids are non-negative in every "
                             "topology this runtime reads.");
};
}  // namespace ctx_numa

namespace ctx_alloc {
struct Unbound {};
struct Stack {};
struct Arena {};
struct Pool {};
struct HugePage {};
struct Heap {};
}  // namespace ctx_alloc

namespace ctx_heat {
struct Cold {};
struct Warm {};
struct Hot {};
}  // namespace ctx_heat

namespace ctx_resid {
struct DRAM {};
struct L3 {};
struct L2 {};
struct L1 {};
}  // namespace ctx_resid

// The termination claim, ordered MayDiverge below Terminating below
// Productive below Bounded.  The bottom claims nothing; the top
// promises a wall-clock budget.
namespace ctx_progress {
struct MayDiverge {};
struct Terminating {};
struct Productive {};
struct Bounded {};
}  // namespace ctx_progress

// What the caller knows about the size of the work ahead.  The
// scheduler reads it when deciding whether fanning out is worth more
// than running the work in place.
namespace ctx_workload {
struct Unspecified {};

template <std::size_t Bytes>
struct ByteBudget {
    static_assert(Bytes > 0, "ctx_workload::ByteBudget<N> requires N > 0.  A context that declares no "
                             "budget uses ctx_workload::Unspecified.");
};

template <std::size_t Items>
struct ItemBudget {
    static_assert(Items > 0, "ctx_workload::ItemBudget<N> requires N > 0.  A context that declares no "
                             "budget uses ctx_workload::Unspecified.");
};

// Choosing an endpoint topology needs more than a byte count: it needs
// the number of parties on each side and whether one-to-many traffic
// keeps only the latest value.
//
// This shape carries both sides.  A channel with only producers or
// only consumers uses one of the two shapes below, so that the absent
// side is visible in the type rather than disguised as a party count
// of one.
template <std::size_t Bytes, std::size_t Producers, std::size_t Consumers, bool LatestOnly = false>
struct ChannelBudget {
    static_assert(Bytes > 0, "ctx_workload::ChannelBudget requires Bytes > 0.  A context that declares "
                             "no budget uses ctx_workload::Unspecified.");
    static_assert(Producers > 0, "ctx_workload::ChannelBudget requires Producers > 0.  A channel read but "
                                 "never written in band uses ctx_workload::ConsumerOnlyChannel.");
    static_assert(Consumers > 0, "ctx_workload::ChannelBudget requires Consumers > 0.  A channel written "
                                 "but never read in band uses ctx_workload::ProducerOnlyChannel.");

    static constexpr std::size_t bytes = Bytes;
    static constexpr std::size_t producers = Producers;
    static constexpr std::size_t consumers = Consumers;
    static constexpr bool latest_only = LatestOnly;
};

// A channel written but never read in band: a telemetry emitter, a
// fire-and-forget announcement, a fan-out leg whose local node is only
// a source.  LatestOnly defaults to true because such a channel almost
// always overwrites rather than queues; a queued one declares a
// consumer side and uses the shape above.
//
// A consumer count of zero tells the topology recommender there is no
// in-band sink, so it cannot choose between the one-to-one and
// one-to-many shapes and falls back to the substrate's own topology.
// The producer count still steers the scheduler.
template <std::size_t Bytes, std::size_t Producers, bool LatestOnly = true>
struct ProducerOnlyChannel {
    static_assert(Bytes > 0, "ctx_workload::ProducerOnlyChannel requires Bytes > 0.  A context that "
                             "declares no budget uses ctx_workload::Unspecified.");
    static_assert(Producers > 0, "ctx_workload::ProducerOnlyChannel requires Producers > 0.");

    static constexpr std::size_t bytes = Bytes;
    static constexpr std::size_t producers = Producers;
    static constexpr std::size_t consumers = 0;
    static constexpr bool latest_only = LatestOnly;
};

// A channel read but never written in band: an audit-log scraper, an
// observer of a buffer some other process fills, a replay from disk.
// There is no LatestOnly parameter, because no in-band producer exists
// to overwrite anything and every consumer reads the whole sequence.
template <std::size_t Bytes, std::size_t Consumers>
struct ConsumerOnlyChannel {
    static_assert(Bytes > 0, "ctx_workload::ConsumerOnlyChannel requires Bytes > 0.  A context that "
                             "declares no budget uses ctx_workload::Unspecified.");
    static_assert(Consumers > 0, "ctx_workload::ConsumerOnlyChannel requires Consumers > 0.");

    static constexpr std::size_t bytes = Bytes;
    static constexpr std::size_t producers = 0;
    static constexpr std::size_t consumers = Consumers;
    static constexpr bool latest_only = false;
};
}  // namespace ctx_workload

template <class T>
struct is_cap_type : std::false_type {};
template <>
struct is_cap_type<ctx_cap::Fg> : std::true_type {};
template <>
struct is_cap_type<Bg> : std::true_type {};
template <>
struct is_cap_type<Init> : std::true_type {};
template <>
struct is_cap_type<Test> : std::true_type {};
template <class T>
inline constexpr bool is_cap_type_v = is_cap_type<T>::value;
template <class T>
concept IsCapType = is_cap_type_v<T>;

template <class T>
struct is_numa_policy : std::false_type {};
template <>
struct is_numa_policy<ctx_numa::Any> : std::true_type {};
template <>
struct is_numa_policy<ctx_numa::Local> : std::true_type {};
template <>
struct is_numa_policy<ctx_numa::Spread> : std::true_type {};
template <int N>
struct is_numa_policy<ctx_numa::Pinned<N>> : std::true_type {};
template <class T>
inline constexpr bool is_numa_policy_v = is_numa_policy<T>::value;
template <class T>
concept IsNumaPolicy = is_numa_policy_v<T>;

template <class T>
struct is_alloc_class : std::false_type {};
template <>
struct is_alloc_class<ctx_alloc::Unbound> : std::true_type {};
template <>
struct is_alloc_class<ctx_alloc::Stack> : std::true_type {};
template <>
struct is_alloc_class<ctx_alloc::Arena> : std::true_type {};
template <>
struct is_alloc_class<ctx_alloc::Pool> : std::true_type {};
template <>
struct is_alloc_class<ctx_alloc::HugePage> : std::true_type {};
template <>
struct is_alloc_class<ctx_alloc::Heap> : std::true_type {};
template <class T>
inline constexpr bool is_alloc_class_v = is_alloc_class<T>::value;
template <class T>
concept IsAllocClass = is_alloc_class_v<T>;

template <class T>
struct is_heat_tier : std::false_type {};
template <>
struct is_heat_tier<ctx_heat::Cold> : std::true_type {};
template <>
struct is_heat_tier<ctx_heat::Warm> : std::true_type {};
template <>
struct is_heat_tier<ctx_heat::Hot> : std::true_type {};
template <class T>
inline constexpr bool is_heat_tier_v = is_heat_tier<T>::value;
template <class T>
concept IsHeatTier = is_heat_tier_v<T>;

template <class T>
struct is_residency_tier : std::false_type {};
template <>
struct is_residency_tier<ctx_resid::DRAM> : std::true_type {};
template <>
struct is_residency_tier<ctx_resid::L3> : std::true_type {};
template <>
struct is_residency_tier<ctx_resid::L2> : std::true_type {};
template <>
struct is_residency_tier<ctx_resid::L1> : std::true_type {};
template <class T>
inline constexpr bool is_residency_tier_v = is_residency_tier<T>::value;
template <class T>
concept IsResidencyTier = is_residency_tier_v<T>;

// Top-level cv and reference are stripped before matching, so that a
// concept fed a forwarding-reference deduction still recognizes the
// row.  Every recognition trait in the project behaves this way.
template <class T>
struct is_effect_row : std::false_type {};
template <Effect... Es>
struct is_effect_row<Row<Es...>> : std::true_type {};
template <class T>
inline constexpr bool is_effect_row_v = is_effect_row<std::remove_cvref_t<T>>::value;
template <class T>
concept IsEffectRow = is_effect_row_v<T>;

template <class T>
struct is_workload_hint : std::false_type {};
template <>
struct is_workload_hint<ctx_workload::Unspecified> : std::true_type {};
template <std::size_t N>
struct is_workload_hint<ctx_workload::ByteBudget<N>> : std::true_type {};
template <std::size_t N>
struct is_workload_hint<ctx_workload::ItemBudget<N>> : std::true_type {};
template <std::size_t Bytes, std::size_t Producers, std::size_t Consumers, bool LatestOnly>
struct is_workload_hint<ctx_workload::ChannelBudget<Bytes, Producers, Consumers, LatestOnly>> : std::true_type {};
template <std::size_t Bytes, std::size_t Producers, bool LatestOnly>
struct is_workload_hint<ctx_workload::ProducerOnlyChannel<Bytes, Producers, LatestOnly>> : std::true_type {};
template <std::size_t Bytes, std::size_t Consumers>
struct is_workload_hint<ctx_workload::ConsumerOnlyChannel<Bytes, Consumers>> : std::true_type {};
template <class T>
inline constexpr bool is_workload_hint_v = is_workload_hint<T>::value;
template <class T>
concept IsWorkloadHint = is_workload_hint_v<T>;

template <class T>
struct is_progress_class : std::false_type {};
template <>
struct is_progress_class<ctx_progress::MayDiverge> : std::true_type {};
template <>
struct is_progress_class<ctx_progress::Terminating> : std::true_type {};
template <>
struct is_progress_class<ctx_progress::Productive> : std::true_type {};
template <>
struct is_progress_class<ctx_progress::Bounded> : std::true_type {};
template <class T>
inline constexpr bool is_progress_class_v = is_progress_class<T>::value;
template <class T>
concept IsProgressClass = is_progress_class_v<T>;

// The largest row each capability source can authorize.  A context's
// own row must stay inside it, which is what stops a foreground
// context from claiming a background effect.
template <class Cap>
struct cap_permitted_row;

template <>
struct cap_permitted_row<ctx_cap::Fg> {
    using type = Row<>;
};
template <>
struct cap_permitted_row<Bg> {
    using type = Row<Effect::Bg, Effect::Alloc, Effect::IO, Effect::Block>;
};
template <>
struct cap_permitted_row<Init> {
    using type = Row<Effect::Init, Effect::Alloc, Effect::IO>;
};
template <>
struct cap_permitted_row<Test> {
    using type = Row<Effect::Test, Effect::Alloc, Effect::IO, Effect::Block>;
};

template <class Cap>
using cap_permitted_row_t = typename cap_permitted_row<Cap>::type;

// The traits below reject combinations that pass their own axis check
// yet contradict each other.  A context is not required to promise
// anything: the default of a cold path over main memory stays valid.

// A hot path cannot reach main memory or the last-level cache and
// still be a hot path, and a warm one cannot reach main memory.
template <class Heat, class Resid>
struct heat_resid_coherent : std::true_type {};
template <>
struct heat_resid_coherent<ctx_heat::Hot, ctx_resid::DRAM> : std::false_type {};
template <>
struct heat_resid_coherent<ctx_heat::Hot, ctx_resid::L3> : std::false_type {};
template <>
struct heat_resid_coherent<ctx_heat::Warm, ctx_resid::DRAM> : std::false_type {};

template <class Heat, class Resid>
inline constexpr bool heat_resid_coherent_v = heat_resid_coherent<Heat, Resid>::value;

// A hot path cannot round-trip through the general allocator.  The
// other allocator classes hand back memory in bounded time.
template <class Heat, class Alloc>
struct heat_alloc_coherent : std::true_type {};
template <>
struct heat_alloc_coherent<ctx_heat::Hot, ctx_alloc::Heap> : std::false_type {};

template <class Heat, class Alloc>
inline constexpr bool heat_alloc_coherent_v = heat_alloc_coherent<Heat, Alloc>::value;

// A hot path must promise to return.  One that may not blocks the
// foreground thread and every latency bound behind it.  A warm or cold
// path may decline the promise: a drain loop can diverge in degenerate
// cases, and an initialization path often has no static proof.
template <class Heat, class Progress>
struct heat_progress_coherent : std::true_type {};
template <>
struct heat_progress_coherent<ctx_heat::Hot, ctx_progress::MayDiverge> : std::false_type {};

template <class Heat, class Progress>
inline constexpr bool heat_progress_coherent_v = heat_progress_coherent<Heat, Progress>::value;

// The axis checks are folded into one concept so that a single bad
// argument reports one diagnostic.  Conjunction short-circuits at the
// first failing atom, which also stops the Subrow atom from being
// substituted and asking a non-capability type for its permitted row.
//
// The concept is checked by a static_assert in the class body rather
// than by a constraint on the template head.  The friend declarations
// that name this template sit in the header that declares the
// capability contexts, and that header cannot include the row algebra
// without a cycle, so it could not repeat a matching constraint.
//
// The cross-axis coherence rules are deliberately left out.  A builder
// chain passes through states that violate them between swapping one
// axis and repairing the other, and folding them in here would reject
// reshapes that callers depend on.
template <class Cap, class Numa, class Alloc, class Heat, class Resid, class Row, class Workload, class Progress>
concept WellFormedExecCtxAxes = IsCapType<Cap> && IsNumaPolicy<Numa> && IsAllocClass<Alloc> && IsHeatTier<Heat>
                             && IsResidencyTier<Resid> && IsEffectRow<Row> && IsWorkloadHint<Workload>
                             && IsProgressClass<Progress> && Subrow<Row, cap_permitted_row_t<Cap>>;

template <class Cap = ctx_cap::Fg, class Numa = ctx_numa::Any, class Alloc = ctx_alloc::Unbound,
          class Heat = ctx_heat::Cold, class Resid = ctx_resid::DRAM, class Row = ::crucible::effects::Row<>,
          class Workload = ctx_workload::Unspecified, class Progress = ctx_progress::Terminating>
class [[nodiscard]] ExecCtx {
public:
    static_assert(WellFormedExecCtxAxes<Cap, Numa, Alloc, Heat, Resid, Row, Workload, Progress>,
                  "One argument to ExecCtx is not a member of its axis, or the row exceeds what the "
                  "capability source permits.  The axes are: a capability source (ctx_cap::Fg, Bg, Init, "
                  "Test); a numa policy (ctx_numa::Any, Local, Spread, Pinned<N>); an alloc class "
                  "(ctx_alloc::Unbound, Stack, Arena, Pool, HugePage, Heap); a heat tier (ctx_heat::Cold, "
                  "Warm, Hot); a residency tier (ctx_resid::DRAM, L3, L2, L1); an effect Row; a workload "
                  "hint (ctx_workload::Unspecified, ByteBudget<N>, ItemBudget<N>, ChannelBudget<...>); and "
                  "a progress class (ctx_progress::MayDiverge, Terminating, Productive, Bounded).");

    static_assert(heat_resid_coherent_v<Heat, Resid>,
                  "The heat tier and the residency tier disagree.  A hot context must sit in L1 or L2, a "
                  "warm one in L1, L2 or L3, and only a cold one may target main memory.");

    static_assert(heat_alloc_coherent_v<Heat, Alloc>,
                  "The heat tier and the alloc class disagree.  A hot context must not use the general "
                  "allocator.  Use Stack, Arena, Pool or HugePage.");

    static_assert(heat_progress_coherent_v<Heat, Progress>,
                  "The heat tier and the progress class disagree.  A hot context must promise to return, "
                  "because one that may not blocks the foreground thread.  Use Terminating, Productive or "
                  "Bounded.");

private:
    // The axis members are private, and the capability member is why.
    // It holds a context whose own default constructor is private and
    // friended here, so exposing the member would let any translation
    // unit copy out a capability context that it could not have
    // constructed for itself.
    //
    // Making this a class rather than a struct changes only the default
    // member access, so the implicit default constructor stays public
    // and the aliases and builder methods below still work.
    [[no_unique_address]] Cap cap_{};
    [[no_unique_address]] Numa numa_{};
    [[no_unique_address]] Alloc alloc_{};
    [[no_unique_address]] Heat heat_{};
    [[no_unique_address]] Resid resid_{};
    [[no_unique_address]] Row row_{};
    [[no_unique_address]] Workload wl_{};
    [[no_unique_address]] Progress progress_{};

public:
    // The only way to reach the capability, and it borrows rather than
    // copies.  Code that wants a copy has to write one, which a grep
    // for this accessor finds.
    [[nodiscard]] constexpr Cap const& cap() const noexcept { return cap_; }

    using cap_type = Cap;
    using numa_policy = Numa;
    using alloc_class = Alloc;
    using hot_path_tier = Heat;
    using residency = Resid;
    using row_type = Row;
    using workload_hint = Workload;
    using progress_class = Progress;

    // Each builder returns a fresh context with one axis replaced.
    // Every link of a chain is a distinct type and every link is one
    // byte.
    template <class NewCap>
        requires IsCapType<NewCap> && Subrow<Row, cap_permitted_row_t<NewCap>>
    [[nodiscard]] consteval auto with_cap() const noexcept
        -> ExecCtx<NewCap, Numa, Alloc, Heat, Resid, Row, Workload, Progress> {
        return {};
    }

    template <class NewNuma>
        requires IsNumaPolicy<NewNuma>
    [[nodiscard]] consteval auto pinned_to() const noexcept
        -> ExecCtx<Cap, NewNuma, Alloc, Heat, Resid, Row, Workload, Progress> {
        return {};
    }

    template <class NewAlloc>
        requires IsAllocClass<NewAlloc>
    [[nodiscard]] consteval auto with_alloc() const noexcept
        -> ExecCtx<Cap, Numa, NewAlloc, Heat, Resid, Row, Workload, Progress> {
        return {};
    }

    template <class NewHeat>
        requires IsHeatTier<NewHeat>
    [[nodiscard]] consteval auto with_heat() const noexcept
        -> ExecCtx<Cap, Numa, Alloc, NewHeat, Resid, Row, Workload, Progress> {
        return {};
    }

    template <class NewResid>
        requires IsResidencyTier<NewResid>
    [[nodiscard]] consteval auto with_residency() const noexcept
        -> ExecCtx<Cap, Numa, Alloc, Heat, NewResid, Row, Workload, Progress> {
        return {};
    }

    // The row only grows.  It may not grow past what the capability
    // source permits, so no chain of calls turns a foreground context
    // into one that claims a background effect.
    template <class NewRow>
        requires IsEffectRow<NewRow> && Subrow<Row, NewRow> && Subrow<NewRow, cap_permitted_row_t<Cap>>
    [[nodiscard]] consteval auto in_row() const noexcept
        -> ExecCtx<Cap, Numa, Alloc, Heat, Resid, NewRow, Workload, Progress> {
        return {};
    }

    template <class NewWl>
        requires IsWorkloadHint<NewWl>
    [[nodiscard]] consteval auto with_workload() const noexcept
        -> ExecCtx<Cap, Numa, Alloc, Heat, Resid, Row, NewWl, Progress> {
        return {};
    }

    // Any progress claim is accepted here, in either direction.  The
    // lattice order is checked where the claim is consumed, not where
    // the context records it.
    template <class NewProgress>
        requires IsProgressClass<NewProgress>
    [[nodiscard]] consteval auto with_progress() const noexcept
        -> ExecCtx<Cap, Numa, Alloc, Heat, Resid, Row, Workload, NewProgress> {
        return {};
    }

    [[nodiscard]] static consteval std::string_view kind_name() noexcept { return "ExecCtx"; }
};

// Naming the common shapes keeps two spellings of the same context
// from drifting apart, and makes each shape findable.

// The context of the foreground thread that runs dispatch.  Its
// progress claim is spelled out rather than defaulted, because a hot
// tier and an absent claim contradict each other.
using HotFgCtx = ExecCtx<ctx_cap::Fg, ctx_numa::Local, ctx_alloc::Stack, ctx_heat::Hot, ctx_resid::L1, Row<>,
                         ctx_workload::Unspecified, ctx_progress::Terminating>;

using BgDrainCtx =
    ExecCtx<Bg, ctx_numa::Local, ctx_alloc::Arena, ctx_heat::Warm, ctx_resid::L2, Row<Effect::Bg, Effect::Alloc>>;

// The compile context claims IO on top of the drain row, because
// compiling writes kernel artifacts.
using BgCompileCtx = ExecCtx<Bg, ctx_numa::Local, ctx_alloc::Arena, ctx_heat::Warm, ctx_resid::L2,
                             Row<Effect::Bg, Effect::Alloc, Effect::IO>>;

// The context of process startup, before the threads are pinned.
using ColdInitCtx = ExecCtx<Init, ctx_numa::Spread, ctx_alloc::Heap, ctx_heat::Cold, ctx_resid::DRAM,
                            Row<Effect::Init, Effect::Alloc, Effect::IO>>;

// A fixture may claim any effect this row names, and no others.  In
// particular it cannot claim the background or initialization effects,
// so it cannot stand in for either of those contexts.
using TestRunnerCtx = ExecCtx<Test, ctx_numa::Any, ctx_alloc::Heap, ctx_heat::Cold, ctx_resid::DRAM,
                              Row<Effect::Test, Effect::Alloc, Effect::IO, Effect::Block>>;

// Top-level cv and reference are stripped before matching, so that a
// concept fed a forwarding-reference deduction still recognizes the
// context.
template <class T>
struct is_exec_ctx : std::false_type {};
template <class Cap, class Numa, class Alloc, class Heat, class Resid, class Row, class Workload, class Progress>
struct is_exec_ctx<ExecCtx<Cap, Numa, Alloc, Heat, Resid, Row, Workload, Progress>> : std::true_type {};
template <class T>
inline constexpr bool is_exec_ctx_v = is_exec_ctx<std::remove_cvref_t<T>>::value;
template <class T>
concept IsExecCtx = is_exec_ctx_v<T>;

template <IsExecCtx Ctx>
using cap_type_of_t = typename Ctx::cap_type;
template <IsExecCtx Ctx>
using numa_policy_of_t = typename Ctx::numa_policy;
template <IsExecCtx Ctx>
using alloc_class_of_t = typename Ctx::alloc_class;
template <IsExecCtx Ctx>
using hot_path_tier_of_t = typename Ctx::hot_path_tier;
template <IsExecCtx Ctx>
using residency_of_t = typename Ctx::residency;
template <IsExecCtx Ctx>
using row_type_of_t = typename Ctx::row_type;
template <IsExecCtx Ctx>
using workload_hint_of_t = typename Ctx::workload_hint;
template <IsExecCtx Ctx>
using progress_class_of_t = typename Ctx::progress_class;

template <class Ctx, class WantCap>
concept HasCap = IsExecCtx<Ctx> && std::is_same_v<cap_type_of_t<Ctx>, WantCap>;

template <class Ctx, class WantNuma>
concept HasNumaPolicy = IsExecCtx<Ctx> && std::is_same_v<numa_policy_of_t<Ctx>, WantNuma>;

template <class Ctx>
concept IsFgCtx = HasCap<Ctx, ctx_cap::Fg>;
template <class Ctx>
concept IsBgCtx = HasCap<Ctx, Bg>;
template <class Ctx>
concept IsInitCtx = HasCap<Ctx, Init>;
template <class Ctx>
concept IsTestCtx = HasCap<Ctx, Test>;

template <class Ctx>
concept IsHotCtx = IsExecCtx<Ctx> && std::is_same_v<hot_path_tier_of_t<Ctx>, ctx_heat::Hot>;
template <class Ctx>
concept IsWarmCtx = IsExecCtx<Ctx> && std::is_same_v<hot_path_tier_of_t<Ctx>, ctx_heat::Warm>;
template <class Ctx>
concept IsColdCtx = IsExecCtx<Ctx> && std::is_same_v<hot_path_tier_of_t<Ctx>, ctx_heat::Cold>;

template <class Ctx>
concept IsArenaCtx = IsExecCtx<Ctx> && std::is_same_v<alloc_class_of_t<Ctx>, ctx_alloc::Arena>;
template <class Ctx>
concept IsHugePageCtx = IsExecCtx<Ctx> && std::is_same_v<alloc_class_of_t<Ctx>, ctx_alloc::HugePage>;
template <class Ctx>
concept IsHeapCtx = IsExecCtx<Ctx> && std::is_same_v<alloc_class_of_t<Ctx>, ctx_alloc::Heap>;
template <class Ctx>
concept IsStackCtx = IsExecCtx<Ctx> && std::is_same_v<alloc_class_of_t<Ctx>, ctx_alloc::Stack>;
template <class Ctx>
concept IsPoolCtx = IsExecCtx<Ctx> && std::is_same_v<alloc_class_of_t<Ctx>, ctx_alloc::Pool>;

template <class Ctx>
concept IsMayDivergeCtx = IsExecCtx<Ctx> && std::is_same_v<progress_class_of_t<Ctx>, ctx_progress::MayDiverge>;
template <class Ctx>
concept IsTerminatingCtx = IsExecCtx<Ctx> && std::is_same_v<progress_class_of_t<Ctx>, ctx_progress::Terminating>;
template <class Ctx>
concept IsProductiveCtx = IsExecCtx<Ctx> && std::is_same_v<progress_class_of_t<Ctx>, ctx_progress::Productive>;
template <class Ctx>
concept IsBoundedCtx = IsExecCtx<Ctx> && std::is_same_v<progress_class_of_t<Ctx>, ctx_progress::Bounded>;

// A body that claims row R may run in a context whose row covers R.
template <class Ctx, class R>
concept CtxAdmits = IsExecCtx<Ctx> && IsEffectRow<R> && Subrow<R, row_type_of_t<Ctx>>;

// A child task forked from a parent must not claim more than the
// parent already claims, and must run under the same capability.
template <class Child, class Parent>
concept IsSubCtx = IsExecCtx<Child> && IsExecCtx<Parent> && std::is_same_v<cap_type_of_t<Child>, cap_type_of_t<Parent>>
                && Subrow<row_type_of_t<Child>, row_type_of_t<Parent>>;

template <class A, class B>
concept SiblingCtx = IsExecCtx<A> && IsExecCtx<B> && std::is_same_v<cap_type_of_t<A>, cap_type_of_t<B>>;

template <class Ctx, Effect Cap>
concept CtxOwnsCapability = IsExecCtx<Ctx> && row_contains_v<row_type_of_t<Ctx>, Cap>;

// The two named lifts below cost exactly what writing the fold by hand
// costs.  What they buy is that a reviewer recognizes the shape of an
// authorization at a glance, and that a rename of the row extractor
// reaches every site through one definition.
template <class Ctx, Effect... Es>
concept CtxOwnsAnyOf = IsExecCtx<Ctx> && (row_contains_v<row_type_of_t<Ctx>, Es> || ...);

template <class Ctx, Effect... Es>
concept CtxOwnsAllOf = IsExecCtx<Ctx> && (row_contains_v<row_type_of_t<Ctx>, Es> && ...);

static_assert(CtxOwnsAnyOf<BgDrainCtx, Effect::Bg, Effect::IO>,
              "The disjunctive lift must accept a row that carries one of the named atoms.");
static_assert(CtxOwnsAnyOf<ColdInitCtx, Effect::Bg, Effect::Init>,
              "The disjunctive lift must accept a row that carries one of the named atoms.");
static_assert(!CtxOwnsAnyOf<HotFgCtx, Effect::Bg, Effect::IO, Effect::Init>,
              "The disjunctive lift must reject an empty row.");
static_assert(!CtxOwnsAnyOf<BgDrainCtx>, "The disjunctive lift over no atoms is false.");

static_assert(CtxOwnsAllOf<BgDrainCtx, Effect::Bg, Effect::Alloc>,
              "The conjunctive lift must accept a row that carries every named atom.");
static_assert(!CtxOwnsAllOf<BgDrainCtx, Effect::Bg, Effect::IO>,
              "The conjunctive lift must reject a row that is missing one of the named atoms.  Widening "
              "the row first is the way through.");
static_assert(CtxOwnsAllOf<BgDrainCtx>, "The conjunctive lift over no atoms is true.");
static_assert(!CtxOwnsAllOf<HotFgCtx, Effect::Bg>,
              "The conjunctive lift must reject any non-empty pack against an empty row.");

// A one-atom lift must answer exactly what the single-atom concept
// answers, so that the two surfaces stay interchangeable.
static_assert(CtxOwnsAnyOf<BgDrainCtx, Effect::Bg> == CtxOwnsCapability<BgDrainCtx, Effect::Bg>,
              "A one-atom disjunctive lift must agree with CtxOwnsCapability.");
static_assert(CtxOwnsAllOf<BgDrainCtx, Effect::Bg> == CtxOwnsCapability<BgDrainCtx, Effect::Bg>,
              "A one-atom conjunctive lift must agree with CtxOwnsCapability.");

// This asks what the capability source could authorize, not what the
// context currently claims.  The two differ: a background context may
// claim only two effects while its source permits four, and the
// context can widen into them.
template <class Ctx, Effect E>
concept CtxCanMint = IsExecCtx<Ctx> && row_contains_v<cap_permitted_row_t<cap_type_of_t<Ctx>>, E>;

// The axes here are tag types; the value wrappers elsewhere hold the
// matching lattice enums.  These project one onto the other.
//
// The residency mapping is not one to one: the two innermost cache
// levels both become the hot residency value.  The unbound allocator
// has no mapping at all, so an attempt to bridge it fails to compile,
// which is right for a context that committed to no policy.
template <class HeatTag>
struct to_hot_path_tier;
template <>
struct to_hot_path_tier<ctx_heat::Hot> {
    static constexpr auto value = ::crucible::algebra::lattices::HotPathTier::Hot;
};
template <>
struct to_hot_path_tier<ctx_heat::Warm> {
    static constexpr auto value = ::crucible::algebra::lattices::HotPathTier::Warm;
};
template <>
struct to_hot_path_tier<ctx_heat::Cold> {
    static constexpr auto value = ::crucible::algebra::lattices::HotPathTier::Cold;
};
template <class HeatTag>
inline constexpr auto to_hot_path_tier_v = to_hot_path_tier<HeatTag>::value;

template <class AllocTag>
struct to_alloc_class_tag;
template <>
struct to_alloc_class_tag<ctx_alloc::Stack> {
    static constexpr auto value = ::crucible::algebra::lattices::AllocClassTag::Stack;
};
template <>
struct to_alloc_class_tag<ctx_alloc::Arena> {
    static constexpr auto value = ::crucible::algebra::lattices::AllocClassTag::Arena;
};
template <>
struct to_alloc_class_tag<ctx_alloc::Pool> {
    static constexpr auto value = ::crucible::algebra::lattices::AllocClassTag::Pool;
};
template <>
struct to_alloc_class_tag<ctx_alloc::Heap> {
    static constexpr auto value = ::crucible::algebra::lattices::AllocClassTag::Heap;
};
template <>
struct to_alloc_class_tag<ctx_alloc::HugePage> {
    static constexpr auto value = ::crucible::algebra::lattices::AllocClassTag::HugePage;
};
template <class AllocTag>
inline constexpr auto to_alloc_class_tag_v = to_alloc_class_tag<AllocTag>::value;

template <class ResidTag>
struct to_residency_heat_tag;
template <>
struct to_residency_heat_tag<ctx_resid::L1> {
    static constexpr auto value = ::crucible::algebra::lattices::ResidencyHeatTag::Hot;
};
template <>
struct to_residency_heat_tag<ctx_resid::L2> {
    static constexpr auto value = ::crucible::algebra::lattices::ResidencyHeatTag::Hot;
};
template <>
struct to_residency_heat_tag<ctx_resid::L3> {
    static constexpr auto value = ::crucible::algebra::lattices::ResidencyHeatTag::Warm;
};
template <>
struct to_residency_heat_tag<ctx_resid::DRAM> {
    static constexpr auto value = ::crucible::algebra::lattices::ResidencyHeatTag::Cold;
};
template <class ResidTag>
inline constexpr auto to_residency_heat_tag_v = to_residency_heat_tag<ResidTag>::value;

// A builder chain evaluates the cross-axis rules at every link, so a
// chain that raises the heat tier before repairing the residency fires
// on the state in between.  This reaches the destination in one step.
// Soundness holds because the destination type still checks its own
// rules when it is instantiated.
//
// The result is the same value as default-constructing the
// destination.  What the call adds is the statement that the new
// context derives from the old one.
//
// It is a free function so that the source type need not be spelled.
template <class NewCtx, IsExecCtx OldCtx>
    requires IsExecCtx<NewCtx>
[[nodiscard]] consteval NewCtx rebuild_ctx_to(OldCtx const&) noexcept {
    return NewCtx{};
}

namespace detail::exec_ctx_self_test {

static_assert(sizeof(ExecCtx<>) == 1, "Every axis of ExecCtx is an empty type, so the whole context must "
                                      "be 1 byte.");
static_assert(sizeof(HotFgCtx) == 1, "The foreground context must be 1 byte");
static_assert(sizeof(BgDrainCtx) == 1, "The background drain context must be 1 byte");
static_assert(sizeof(BgCompileCtx) == 1, "The background compile context must be 1 byte");
static_assert(sizeof(ColdInitCtx) == 1, "The initialization context must be 1 byte");
static_assert(sizeof(TestRunnerCtx) == 1, "The test runner context must be 1 byte");

using MaxCtx = ExecCtx<Bg, ctx_numa::Pinned<3>, ctx_alloc::HugePage, ctx_heat::Hot, ctx_resid::L1,
                       Row<Effect::Bg, Effect::Alloc, Effect::IO, Effect::Block>,
                       ctx_workload::ByteBudget<2 * 1024 * 1024>, ctx_progress::Bounded>;
static_assert(sizeof(MaxCtx) == 1, "A context that names a distinct tag on every axis must still be 1 byte");

// Every axis defaults to the claim-nothing end of its range, except
// progress.  That one defaults to Terminating because a C++ function
// is expected to return, so declining the promise is the deliberate
// case.  The choice also lets a hot context leave progress alone
// instead of restating it at every site.
static_assert(std::is_same_v<typename ExecCtx<>::cap_type, ctx_cap::Fg>);
static_assert(std::is_same_v<typename ExecCtx<>::numa_policy, ctx_numa::Any>);
static_assert(std::is_same_v<typename ExecCtx<>::alloc_class, ctx_alloc::Unbound>);
static_assert(std::is_same_v<typename ExecCtx<>::hot_path_tier, ctx_heat::Cold>);
static_assert(std::is_same_v<typename ExecCtx<>::residency, ctx_resid::DRAM>);
static_assert(std::is_same_v<typename ExecCtx<>::row_type, Row<>>);
static_assert(std::is_same_v<typename ExecCtx<>::workload_hint, ctx_workload::Unspecified>);
static_assert(std::is_same_v<typename ExecCtx<>::progress_class, ctx_progress::Terminating>);

static_assert(std::is_same_v<typename HotFgCtx::cap_type, ctx_cap::Fg>);
static_assert(std::is_same_v<typename HotFgCtx::numa_policy, ctx_numa::Local>);
static_assert(std::is_same_v<typename HotFgCtx::alloc_class, ctx_alloc::Stack>);
static_assert(std::is_same_v<typename HotFgCtx::hot_path_tier, ctx_heat::Hot>);
static_assert(std::is_same_v<typename HotFgCtx::residency, ctx_resid::L1>);
static_assert(std::is_same_v<typename HotFgCtx::row_type, Row<>>);
static_assert(std::is_same_v<typename HotFgCtx::workload_hint, ctx_workload::Unspecified>);
static_assert(std::is_same_v<typename HotFgCtx::progress_class, ctx_progress::Terminating>);

constexpr auto _ctx0 = ExecCtx<>{};
constexpr auto _ctx1 = _ctx0.with_cap<Bg>();
static_assert(std::is_same_v<typename decltype(_ctx1)::cap_type, Bg>);
static_assert(std::is_same_v<typename decltype(_ctx1)::numa_policy, ctx_numa::Any>);
static_assert(std::is_same_v<typename decltype(_ctx1)::alloc_class, ctx_alloc::Unbound>);
static_assert(std::is_same_v<typename decltype(_ctx1)::hot_path_tier, ctx_heat::Cold>);

constexpr auto _ctx2 = _ctx1.pinned_to<ctx_numa::Pinned<3>>();
static_assert(std::is_same_v<typename decltype(_ctx2)::numa_policy, ctx_numa::Pinned<3>>);

// Residency and progress are set before heat.  Raising heat first
// would fire the cross-axis rules against the state in between, where
// the other two axes still hold their defaults.
constexpr auto _ctx3 = _ctx2.with_alloc<ctx_alloc::Arena>()
                           .with_residency<ctx_resid::L1>()
                           .with_progress<ctx_progress::Terminating>()
                           .with_heat<ctx_heat::Hot>();
static_assert(std::is_same_v<typename decltype(_ctx3)::alloc_class, ctx_alloc::Arena>);
static_assert(std::is_same_v<typename decltype(_ctx3)::hot_path_tier, ctx_heat::Hot>);
static_assert(std::is_same_v<typename decltype(_ctx3)::residency, ctx_resid::L1>);
static_assert(std::is_same_v<typename decltype(_ctx3)::progress_class, ctx_progress::Terminating>);

constexpr auto _ctx4 = _ctx3.in_row<Row<Effect::Bg, Effect::Alloc>>();
static_assert(std::is_same_v<typename decltype(_ctx4)::row_type, Row<Effect::Bg, Effect::Alloc>>);

constexpr auto _ctx5 = _ctx4.in_row<Row<Effect::Bg, Effect::Alloc, Effect::IO>>();
static_assert(std::is_same_v<typename decltype(_ctx5)::row_type, Row<Effect::Bg, Effect::Alloc, Effect::IO>>);

constexpr auto _ctx6 = _ctx5.with_workload<ctx_workload::ByteBudget<4096>>();
static_assert(std::is_same_v<typename decltype(_ctx6)::workload_hint, ctx_workload::ByteBudget<4096>>);
static_assert(std::is_same_v<typename decltype(_ctx6)::cap_type, Bg>);
static_assert(std::is_same_v<typename decltype(_ctx6)::numa_policy, ctx_numa::Pinned<3>>);

constexpr auto _ctx7 = _ctx6.with_progress<ctx_progress::Bounded>();
static_assert(std::is_same_v<typename decltype(_ctx7)::progress_class, ctx_progress::Bounded>);

static_assert(sizeof(_ctx0) == 1);
static_assert(sizeof(_ctx1) == 1);
static_assert(sizeof(_ctx2) == 1);
static_assert(sizeof(_ctx3) == 1);
static_assert(sizeof(_ctx4) == 1);
static_assert(sizeof(_ctx5) == 1);
static_assert(sizeof(_ctx6) == 1);
static_assert(sizeof(_ctx7) == 1);

static_assert(std::is_same_v<typename BgDrainCtx::cap_type, Bg>);
static_assert(std::is_same_v<typename BgDrainCtx::numa_policy, ctx_numa::Local>);
static_assert(std::is_same_v<typename BgDrainCtx::alloc_class, ctx_alloc::Arena>);
static_assert(std::is_same_v<typename BgDrainCtx::row_type, Row<Effect::Bg, Effect::Alloc>>);

static_assert(std::is_same_v<typename BgCompileCtx::row_type, Row<Effect::Bg, Effect::Alloc, Effect::IO>>);

static_assert(std::is_same_v<typename ColdInitCtx::cap_type, Init>);
static_assert(std::is_same_v<typename ColdInitCtx::numa_policy, ctx_numa::Spread>);

static_assert(std::is_same_v<typename TestRunnerCtx::cap_type, Test>);

using BgVariantA = ExecCtx<Bg, ctx_numa::Local, ctx_alloc::Arena>;
using BgVariantB = ExecCtx<Bg, ctx_numa::Spread, ctx_alloc::Arena>;
static_assert(!std::is_same_v<BgVariantA, BgVariantB>,
              "Two contexts that differ in numa policy must be distinct types");

using HeatLow = ExecCtx<Bg, ctx_numa::Local, ctx_alloc::Arena, ctx_heat::Cold>;
using HeatHigh = ExecCtx<Bg, ctx_numa::Local, ctx_alloc::Arena, ctx_heat::Hot>;
static_assert(!std::is_same_v<HeatLow, HeatHigh>, "Two contexts that differ in heat tier must be distinct types");

static_assert(is_cap_type_v<ctx_cap::Fg>);
static_assert(is_cap_type_v<Bg>);
static_assert(is_cap_type_v<Init>);
static_assert(is_cap_type_v<Test>);
static_assert(!is_cap_type_v<int>);
static_assert(!is_cap_type_v<void*>);

static_assert(is_numa_policy_v<ctx_numa::Any>);
static_assert(is_numa_policy_v<ctx_numa::Local>);
static_assert(is_numa_policy_v<ctx_numa::Spread>);
static_assert(is_numa_policy_v<ctx_numa::Pinned<3>>);
// A negative node is recognized as the policy shape here.  The node
// value itself is rejected inside the template.
static_assert(is_numa_policy_v<ctx_numa::Pinned<-1>>);
static_assert(!is_numa_policy_v<int>);

static_assert(is_alloc_class_v<ctx_alloc::Arena>);
static_assert(!is_alloc_class_v<int>);

static_assert(is_heat_tier_v<ctx_heat::Hot>);
static_assert(!is_heat_tier_v<int>);

static_assert(is_residency_tier_v<ctx_resid::L1>);
static_assert(!is_residency_tier_v<int>);

static_assert(is_effect_row_v<Row<>>);
static_assert(is_effect_row_v<Row<Effect::Bg, Effect::Alloc>>);
static_assert(!is_effect_row_v<int>);

static_assert(is_effect_row_v<Row<> const>);
static_assert(is_effect_row_v<Row<>&>);
static_assert(is_effect_row_v<Row<Effect::Bg> const&>);
static_assert(is_effect_row_v<Row<Effect::Bg>&&>);
static_assert(!is_effect_row_v<int const&>);
static_assert(IsEffectRow<Row<Effect::Bg> const&>);
static_assert(IsEffectRow<Row<Effect::Bg>&&>);

static_assert(is_workload_hint_v<ctx_workload::Unspecified>);
static_assert(is_workload_hint_v<ctx_workload::ByteBudget<4096>>);
static_assert(is_workload_hint_v<ctx_workload::ItemBudget<128>>);
static_assert(!is_workload_hint_v<int>);

static_assert(is_progress_class_v<ctx_progress::MayDiverge>);
static_assert(is_progress_class_v<ctx_progress::Terminating>);
static_assert(is_progress_class_v<ctx_progress::Productive>);
static_assert(is_progress_class_v<ctx_progress::Bounded>);
static_assert(!is_progress_class_v<int>);
static_assert(!is_progress_class_v<void*>);

static_assert(std::is_same_v<cap_permitted_row_t<ctx_cap::Fg>, Row<>>);
static_assert(std::is_same_v<cap_permitted_row_t<Bg>, Row<Effect::Bg, Effect::Alloc, Effect::IO, Effect::Block>>);
static_assert(std::is_same_v<cap_permitted_row_t<Init>, Row<Effect::Init, Effect::Alloc, Effect::IO>>);
static_assert(std::is_same_v<cap_permitted_row_t<Test>, Row<Effect::Test, Effect::Alloc, Effect::IO, Effect::Block>>);

// The aliases already satisfy this, or their own declarations would
// have failed.  Restating it here catches a later rewrite of an alias.
static_assert(Subrow<typename HotFgCtx::row_type, cap_permitted_row_t<typename HotFgCtx::cap_type>>);
static_assert(Subrow<typename BgDrainCtx::row_type, cap_permitted_row_t<typename BgDrainCtx::cap_type>>);
static_assert(Subrow<typename BgCompileCtx::row_type, cap_permitted_row_t<typename BgCompileCtx::cap_type>>);
static_assert(Subrow<typename ColdInitCtx::row_type, cap_permitted_row_t<typename ColdInitCtx::cap_type>>);
static_assert(Subrow<typename TestRunnerCtx::row_type, cap_permitted_row_t<typename TestRunnerCtx::cap_type>>);

static_assert(is_exec_ctx_v<HotFgCtx>);
static_assert(is_exec_ctx_v<BgDrainCtx>);
static_assert(is_exec_ctx_v<MaxCtx>);
static_assert(!is_exec_ctx_v<int>);
static_assert(!is_exec_ctx_v<Bg>);

static_assert(is_exec_ctx_v<HotFgCtx const>);
static_assert(is_exec_ctx_v<HotFgCtx&>);
static_assert(is_exec_ctx_v<HotFgCtx const&>);
static_assert(is_exec_ctx_v<HotFgCtx&&>);
static_assert(is_exec_ctx_v<BgDrainCtx const&>);
static_assert(!is_exec_ctx_v<int const&>);
static_assert(!is_exec_ctx_v<Bg const&>);
static_assert(IsExecCtx<HotFgCtx const&>);
static_assert(IsExecCtx<HotFgCtx&&>);

static_assert(std::is_same_v<cap_type_of_t<BgDrainCtx>, Bg>);
static_assert(std::is_same_v<numa_policy_of_t<BgDrainCtx>, ctx_numa::Local>);
static_assert(std::is_same_v<alloc_class_of_t<BgDrainCtx>, ctx_alloc::Arena>);
static_assert(std::is_same_v<row_type_of_t<BgDrainCtx>, Row<Effect::Bg, Effect::Alloc>>);

static_assert(HasCap<BgDrainCtx, Bg>);
static_assert(!HasCap<BgDrainCtx, ctx_cap::Fg>);
static_assert(HasNumaPolicy<BgDrainCtx, ctx_numa::Local>);

static_assert(std::is_same_v<ctx_cap::Bg, Bg>);
static_assert(std::is_same_v<ctx_cap::Init, Init>);
static_assert(std::is_same_v<ctx_cap::Test, Test>);

// Promoting the empty row to a background capability is admitted.
// Moving a background row to an initialization capability is not, and
// would have to narrow the row first.
constexpr auto _bg_promoted = HotFgCtx{}.with_cap<Bg>();
static_assert(std::is_same_v<typename decltype(_bg_promoted)::cap_type, Bg>);
static_assert(std::is_same_v<typename decltype(_bg_promoted)::row_type, Row<>>);

static_assert(heat_resid_coherent_v<ctx_heat::Hot, ctx_resid::L1>);
static_assert(heat_resid_coherent_v<ctx_heat::Hot, ctx_resid::L2>);
static_assert(!heat_resid_coherent_v<ctx_heat::Hot, ctx_resid::L3>);
static_assert(!heat_resid_coherent_v<ctx_heat::Hot, ctx_resid::DRAM>);

static_assert(heat_resid_coherent_v<ctx_heat::Warm, ctx_resid::L1>);
static_assert(heat_resid_coherent_v<ctx_heat::Warm, ctx_resid::L2>);
static_assert(heat_resid_coherent_v<ctx_heat::Warm, ctx_resid::L3>);
static_assert(!heat_resid_coherent_v<ctx_heat::Warm, ctx_resid::DRAM>);

static_assert(heat_resid_coherent_v<ctx_heat::Cold, ctx_resid::L1>);
static_assert(heat_resid_coherent_v<ctx_heat::Cold, ctx_resid::L2>);
static_assert(heat_resid_coherent_v<ctx_heat::Cold, ctx_resid::L3>);
static_assert(heat_resid_coherent_v<ctx_heat::Cold, ctx_resid::DRAM>);

static_assert(!heat_progress_coherent_v<ctx_heat::Hot, ctx_progress::MayDiverge>);
static_assert(heat_progress_coherent_v<ctx_heat::Hot, ctx_progress::Terminating>);
static_assert(heat_progress_coherent_v<ctx_heat::Hot, ctx_progress::Productive>);
static_assert(heat_progress_coherent_v<ctx_heat::Hot, ctx_progress::Bounded>);
static_assert(heat_progress_coherent_v<ctx_heat::Warm, ctx_progress::MayDiverge>);
static_assert(heat_progress_coherent_v<ctx_heat::Warm, ctx_progress::Terminating>);
static_assert(heat_progress_coherent_v<ctx_heat::Cold, ctx_progress::MayDiverge>);
static_assert(heat_progress_coherent_v<ctx_heat::Cold, ctx_progress::Bounded>);

static_assert(heat_alloc_coherent_v<ctx_heat::Hot, ctx_alloc::Stack>);
static_assert(heat_alloc_coherent_v<ctx_heat::Hot, ctx_alloc::Arena>);
static_assert(heat_alloc_coherent_v<ctx_heat::Hot, ctx_alloc::Pool>);
static_assert(heat_alloc_coherent_v<ctx_heat::Hot, ctx_alloc::HugePage>);
static_assert(heat_alloc_coherent_v<ctx_heat::Hot, ctx_alloc::Unbound>);
static_assert(!heat_alloc_coherent_v<ctx_heat::Hot, ctx_alloc::Heap>);
static_assert(heat_alloc_coherent_v<ctx_heat::Warm, ctx_alloc::Heap>);
static_assert(heat_alloc_coherent_v<ctx_heat::Cold, ctx_alloc::Heap>);

// The aliases already satisfy the cross-axis rules, or their own
// declarations would have failed.  Restating the rules here catches a
// later rewrite of an alias.
static_assert(heat_resid_coherent_v<typename HotFgCtx::hot_path_tier, typename HotFgCtx::residency>);
static_assert(heat_resid_coherent_v<typename BgDrainCtx::hot_path_tier, typename BgDrainCtx::residency>);
static_assert(heat_resid_coherent_v<typename BgCompileCtx::hot_path_tier, typename BgCompileCtx::residency>);
static_assert(heat_resid_coherent_v<typename ColdInitCtx::hot_path_tier, typename ColdInitCtx::residency>);
static_assert(heat_resid_coherent_v<typename TestRunnerCtx::hot_path_tier, typename TestRunnerCtx::residency>);
static_assert(heat_resid_coherent_v<typename MaxCtx::hot_path_tier, typename MaxCtx::residency>);

static_assert(heat_alloc_coherent_v<typename HotFgCtx::hot_path_tier, typename HotFgCtx::alloc_class>);
static_assert(heat_alloc_coherent_v<typename BgDrainCtx::hot_path_tier, typename BgDrainCtx::alloc_class>);
static_assert(heat_alloc_coherent_v<typename ColdInitCtx::hot_path_tier, typename ColdInitCtx::alloc_class>);
static_assert(heat_alloc_coherent_v<typename MaxCtx::hot_path_tier, typename MaxCtx::alloc_class>);

static_assert(heat_progress_coherent_v<typename HotFgCtx::hot_path_tier, typename HotFgCtx::progress_class>);
static_assert(heat_progress_coherent_v<typename BgDrainCtx::hot_path_tier, typename BgDrainCtx::progress_class>);
static_assert(heat_progress_coherent_v<typename ColdInitCtx::hot_path_tier, typename ColdInitCtx::progress_class>);
static_assert(heat_progress_coherent_v<typename MaxCtx::hot_path_tier, typename MaxCtx::progress_class>);

static_assert(IsHotCtx<HotFgCtx>);
static_assert(!IsHotCtx<BgDrainCtx>);
static_assert(!IsHotCtx<ColdInitCtx>);

static_assert(IsWarmCtx<BgDrainCtx>);
static_assert(IsWarmCtx<BgCompileCtx>);
static_assert(!IsWarmCtx<HotFgCtx>);

static_assert(IsColdCtx<ColdInitCtx>);
static_assert(IsColdCtx<TestRunnerCtx>);
static_assert(!IsColdCtx<HotFgCtx>);

static_assert(IsFgCtx<HotFgCtx>);
static_assert(!IsFgCtx<BgDrainCtx>);

static_assert(IsBgCtx<BgDrainCtx>);
static_assert(IsBgCtx<BgCompileCtx>);
static_assert(!IsBgCtx<HotFgCtx>);

static_assert(IsInitCtx<ColdInitCtx>);
static_assert(!IsInitCtx<BgDrainCtx>);

static_assert(IsTestCtx<TestRunnerCtx>);
static_assert(!IsTestCtx<BgDrainCtx>);

static_assert(IsArenaCtx<BgDrainCtx>);
static_assert(IsArenaCtx<BgCompileCtx>);
static_assert(!IsArenaCtx<HotFgCtx>);
static_assert(IsHugePageCtx<MaxCtx>);
static_assert(IsHeapCtx<ColdInitCtx>);
static_assert(IsHeapCtx<TestRunnerCtx>);
static_assert(IsStackCtx<HotFgCtx>);

static_assert(IsTerminatingCtx<HotFgCtx>);
static_assert(!IsMayDivergeCtx<HotFgCtx>);
static_assert(IsBoundedCtx<MaxCtx>);
static_assert(!IsTerminatingCtx<MaxCtx>);

// The aliases below name no progress class and so take the default.
static_assert(IsTerminatingCtx<BgDrainCtx>);
static_assert(IsTerminatingCtx<BgCompileCtx>);
static_assert(IsTerminatingCtx<ColdInitCtx>);
static_assert(IsTerminatingCtx<TestRunnerCtx>);

static_assert(CtxAdmits<HotFgCtx, Row<>>);
static_assert(!CtxAdmits<HotFgCtx, Row<Effect::Bg>>);
static_assert(CtxAdmits<BgDrainCtx, Row<Effect::Bg>>);
static_assert(CtxAdmits<BgDrainCtx, Row<Effect::Bg, Effect::Alloc>>);
static_assert(!CtxAdmits<BgDrainCtx, Row<Effect::IO>>);
static_assert(CtxAdmits<BgCompileCtx, Row<Effect::IO>>);
static_assert(CtxAdmits<TestRunnerCtx, Row<Effect::Block>>);

static_assert(IsSubCtx<BgDrainCtx, BgCompileCtx>);
static_assert(!IsSubCtx<BgCompileCtx, BgDrainCtx>);
static_assert(!IsSubCtx<HotFgCtx, BgDrainCtx>);
static_assert(IsSubCtx<HotFgCtx, HotFgCtx>);

static_assert(SiblingCtx<BgDrainCtx, BgCompileCtx>);
static_assert(!SiblingCtx<HotFgCtx, BgDrainCtx>);

static_assert(CtxOwnsCapability<BgDrainCtx, Effect::Bg>);
static_assert(CtxOwnsCapability<BgDrainCtx, Effect::Alloc>);
static_assert(!CtxOwnsCapability<BgDrainCtx, Effect::IO>);
static_assert(CtxOwnsCapability<BgCompileCtx, Effect::IO>);
static_assert(!CtxOwnsCapability<HotFgCtx, Effect::Bg>);

// The background drain context claims two effects but its source
// permits four, so the next two assertions differ from the ones above.
static_assert(CtxCanMint<BgDrainCtx, Effect::Alloc>);
static_assert(CtxCanMint<BgDrainCtx, Effect::IO>);
static_assert(CtxCanMint<BgDrainCtx, Effect::Block>);
static_assert(CtxCanMint<BgDrainCtx, Effect::Bg>);
static_assert(!CtxCanMint<BgDrainCtx, Effect::Init>);
static_assert(CtxCanMint<BgCompileCtx, Effect::Block>);
static_assert(CtxCanMint<ColdInitCtx, Effect::Alloc>);
static_assert(CtxCanMint<ColdInitCtx, Effect::IO>);
static_assert(!CtxCanMint<ColdInitCtx, Effect::Block>);
static_assert(!CtxCanMint<HotFgCtx, Effect::Alloc>);
static_assert(!CtxCanMint<HotFgCtx, Effect::Bg>);
static_assert(CtxCanMint<TestRunnerCtx, Effect::Block>);

namespace lat = ::crucible::algebra::lattices;

static_assert(to_hot_path_tier_v<ctx_heat::Hot> == lat::HotPathTier::Hot);
static_assert(to_hot_path_tier_v<ctx_heat::Warm> == lat::HotPathTier::Warm);
static_assert(to_hot_path_tier_v<ctx_heat::Cold> == lat::HotPathTier::Cold);

static_assert(to_alloc_class_tag_v<ctx_alloc::Stack> == lat::AllocClassTag::Stack);
static_assert(to_alloc_class_tag_v<ctx_alloc::Arena> == lat::AllocClassTag::Arena);
static_assert(to_alloc_class_tag_v<ctx_alloc::Pool> == lat::AllocClassTag::Pool);
static_assert(to_alloc_class_tag_v<ctx_alloc::Heap> == lat::AllocClassTag::Heap);
static_assert(to_alloc_class_tag_v<ctx_alloc::HugePage> == lat::AllocClassTag::HugePage);
static_assert(to_residency_heat_tag_v<ctx_resid::L1> == lat::ResidencyHeatTag::Hot);
static_assert(to_residency_heat_tag_v<ctx_resid::L2> == lat::ResidencyHeatTag::Hot);
static_assert(to_residency_heat_tag_v<ctx_resid::L3> == lat::ResidencyHeatTag::Warm);
static_assert(to_residency_heat_tag_v<ctx_resid::DRAM> == lat::ResidencyHeatTag::Cold);

static_assert(to_hot_path_tier_v<typename HotFgCtx::hot_path_tier> == lat::HotPathTier::Hot);
static_assert(to_alloc_class_tag_v<typename BgDrainCtx::alloc_class> == lat::AllocClassTag::Arena);
static_assert(to_residency_heat_tag_v<typename BgDrainCtx::residency> == lat::ResidencyHeatTag::Hot);
static_assert(to_alloc_class_tag_v<typename MaxCtx::alloc_class> == lat::AllocClassTag::HugePage);

constexpr auto _rebuilt = rebuild_ctx_to<BgDrainCtx>(HotFgCtx{});
static_assert(std::is_same_v<decltype(_rebuilt), const BgDrainCtx>);

// Every operation is driven here with non-constant arguments.  The
// static_assert wall above only proves the constant-evaluated path.
inline void runtime_smoke_test() {
    [[maybe_unused]] HotFgCtx hot{};
    [[maybe_unused]] BgDrainCtx bg{};
    [[maybe_unused]] BgCompileCtx compile{};
    [[maybe_unused]] ColdInitCtx cold{};
    [[maybe_unused]] TestRunnerCtx test_ctx{};

    [[maybe_unused]] auto s1 = sizeof(hot);
    [[maybe_unused]] auto s2 = sizeof(bg);

    auto rebuilt = rebuild_ctx_to<BgDrainCtx>(hot);
    [[maybe_unused]] BgDrainCtx r_copy = rebuilt;

    // Raising the progress claim of a hot context satisfies the
    // cross-axis rule whatever the starting claim was.
    auto hot_bounded = hot.template with_progress<ctx_progress::Bounded>();
    static_assert(std::is_same_v<typename decltype(hot_bounded)::progress_class, ctx_progress::Bounded>);
    [[maybe_unused]] auto s3 = sizeof(hot_bounded);

    auto cold_productive = cold.template with_progress<ctx_progress::Productive>();
    static_assert(std::is_same_v<typename decltype(cold_productive)::progress_class, ctx_progress::Productive>);
    [[maybe_unused]] auto s4 = sizeof(cold_productive);
}

}  // namespace detail::exec_ctx_self_test

}  // namespace crucible::effects
