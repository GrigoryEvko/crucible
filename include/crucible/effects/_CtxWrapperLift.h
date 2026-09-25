#pragma once

// Lifts a context's per-axis tag into the matching wrapper, so a call
// site can wrap a value with the tier of the context it already sits in.
//
// These lifts live apart from the context header because they pull in
// the three Graded-backed wrapper headers.  Folding them in would put
// that transitive weight on every consumer of a context, so a caller
// opts in here instead.

#include <crucible/effects/_ExecCtx.h>
#include <crucible/safety/_AllocClass.h>
#include <crucible/safety/_HotPath.h>
#include <crucible/safety/ResidencyHeat.h>

namespace crucible::effects {

template <IsExecCtx Ctx, class T>
using HotPathFromCtx = ::crucible::safety::HotPath<to_hot_path_tier_v<hot_path_tier_of_t<Ctx>>, T>;

// An unbound allocation class has no tag to lift to, and the bridge
// metafunction's primary template is undefined.  A direct alias would
// therefore fail as a cascade of instantiation errors, so this one goes
// through a trampoline whose assertion fires first and reads plainly.

namespace detail {
template <class AllocT>
inline constexpr bool is_unbound_alloc_v = std::is_same_v<AllocT, ctx_alloc::Unbound>;

template <IsExecCtx Ctx, class T>
struct alloc_class_from_ctx {
    static_assert(!is_unbound_alloc_v<alloc_class_of_t<Ctx>>,
                  "AllocClassFromCtx<Ctx, T> requires Ctx::alloc_class to be "
                  "bound (one of ctx_alloc::{Stack, Arena, Pool, Heap, "
                  "HugePage}).  ctx_alloc::Unbound has no wrapper analogue.");
    using type = ::crucible::safety::AllocClass<to_alloc_class_tag_v<alloc_class_of_t<Ctx>>, T>;
};
}  // namespace detail

template <IsExecCtx Ctx, class T>
using AllocClassFromCtx = typename detail::alloc_class_from_ctx<Ctx, T>::type;

template <IsExecCtx Ctx, class T>
using ResidencyHeatFromCtx = ::crucible::safety::ResidencyHeat<to_residency_heat_tag_v<residency_of_t<Ctx>>, T>;

namespace detail::ctx_wrapper_lift_self_test {

namespace lat = ::crucible::algebra::lattices;
namespace saf = ::crucible::safety;

static_assert(std::is_same_v<HotPathFromCtx<HotFgCtx, int>, saf::HotPath<lat::HotPathTier::Hot, int>>);

static_assert(std::is_same_v<HotPathFromCtx<BgDrainCtx, int>, saf::HotPath<lat::HotPathTier::Warm, int>>);
static_assert(std::is_same_v<HotPathFromCtx<BgCompileCtx, double>, saf::HotPath<lat::HotPathTier::Warm, double>>);

static_assert(std::is_same_v<HotPathFromCtx<ColdInitCtx, void*>, saf::HotPath<lat::HotPathTier::Cold, void*>>);
static_assert(std::is_same_v<HotPathFromCtx<TestRunnerCtx, char>, saf::HotPath<lat::HotPathTier::Cold, char>>);

static_assert(sizeof(HotPathFromCtx<HotFgCtx, int>) == sizeof(int));

static_assert(std::is_same_v<AllocClassFromCtx<HotFgCtx, int*>, saf::AllocClass<lat::AllocClassTag::Stack, int*>>);
static_assert(std::is_same_v<AllocClassFromCtx<BgDrainCtx, void*>, saf::AllocClass<lat::AllocClassTag::Arena, void*>>);
static_assert(std::is_same_v<AllocClassFromCtx<ColdInitCtx, void*>, saf::AllocClass<lat::AllocClassTag::Heap, void*>>);
static_assert(
    std::is_same_v<AllocClassFromCtx<TestRunnerCtx, void*>, saf::AllocClass<lat::AllocClassTag::Heap, void*>>);

static_assert(sizeof(AllocClassFromCtx<BgDrainCtx, void*>) == sizeof(void*));

// Four residency levels collapse onto three heat tags: L1 and L2 both
// map to Hot, L3 to Warm, DRAM to Cold.  Two contexts with different
// residency can therefore lift to the same tag.

static_assert(std::is_same_v<ResidencyHeatFromCtx<HotFgCtx, int>, saf::ResidencyHeat<lat::ResidencyHeatTag::Hot, int>>);
static_assert(
    std::is_same_v<ResidencyHeatFromCtx<BgDrainCtx, int>, saf::ResidencyHeat<lat::ResidencyHeatTag::Hot, int>>);
static_assert(
    std::is_same_v<ResidencyHeatFromCtx<ColdInitCtx, int>, saf::ResidencyHeat<lat::ResidencyHeatTag::Cold, int>>);

static_assert(sizeof(ResidencyHeatFromCtx<HotFgCtx, int>) == sizeof(int));

}  // namespace detail::ctx_wrapper_lift_self_test

// Instantiates each lift, so an alias that resolves to something
// unconstructible fails here rather than at the first call site.
[[gnu::cold]] inline void runtime_smoke_test_ctx_wrapper_lift() noexcept {
    HotPathFromCtx<HotFgCtx, int> hp_hot{42};
    HotPathFromCtx<BgDrainCtx, int> hp_warm{7};
    HotPathFromCtx<ColdInitCtx, int> hp_cold{0};

    AllocClassFromCtx<HotFgCtx, int*> ac_stack{nullptr};
    AllocClassFromCtx<BgDrainCtx, void*> ac_arena{nullptr};
    AllocClassFromCtx<ColdInitCtx, void*> ac_heap{nullptr};

    ResidencyHeatFromCtx<HotFgCtx, int> rh_hot{1};
    ResidencyHeatFromCtx<BgDrainCtx, int> rh_l2{2};
    ResidencyHeatFromCtx<ColdInitCtx, int> rh_dram{3};

    static_cast<void>(hp_hot);
    static_cast<void>(hp_warm);
    static_cast<void>(hp_cold);
    static_cast<void>(ac_stack);
    static_cast<void>(ac_arena);
    static_cast<void>(ac_heap);
    static_cast<void>(rh_hot);
    static_cast<void>(rh_l2);
    static_cast<void>(rh_dram);
}

}  // namespace crucible::effects
