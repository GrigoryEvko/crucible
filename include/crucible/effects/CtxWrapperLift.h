#pragma once

// ── crucible::effects::CtxWrapperLift — Ctx → safety:: wrapper ──────
//
// Aliases that lift a Ctx's per-axis tag into the matching
// safety::* Graded wrapper instantiation.  Production code that
// wants to wrap a value with the surrounding Ctx's tier/class can
// write
//
//     HotPathFromCtx<Ctx, T>      instead of
//     safety::HotPath<to_hot_path_tier_v<typename Ctx::hot_path_tier>, T>
//
// No new traits — these aliases compose the existing wrapper-enum
// bridges (to_hot_path_tier_v / to_alloc_class_tag_v /
// to_residency_heat_tag_v from ExecCtx.h) with the safety:: wrapper
// templates.  Zero runtime cost; pure type-level rewrites.
//
//   Axiom coverage: TypeSafe — Ctx-driven instantiation; mismatched
//                   axis types fail at the bridge metafunction
//                   (already gated in ExecCtx.h).
//                   InitSafe — pure aliases; no construction at this
//                   layer.
//   Runtime cost:   zero.  sizeof(HotPathFromCtx<Ctx, int>) ==
//                   sizeof(int), same as the underlying wrapper.
//
// ── Why a separate header ───────────────────────────────────────────
//
// The lifts pull in safety/HotPath.h, safety/AllocClass.h, and
// safety/ResidencyHeat.h — the Graded-backed wrappers that consume
// the bridged enum values.  Putting them in ExecCtx.h would
// triple ExecCtx's transitive include weight.  Production code
// that wants the lifts opts in via this header explicitly.
//
// ── Coverage ────────────────────────────────────────────────────────
//
//   HotPathFromCtx<Ctx, T>       → safety::HotPath<…, T>
//   AllocClassFromCtx<Ctx, T>    → safety::AllocClass<…, T>  [requires bound alloc]
//   ResidencyHeatFromCtx<Ctx, T> → safety::ResidencyHeat<…, T>
//
// Note: AllocClassFromCtx requires the Ctx's alloc_class to be
// bound (anything but ctx_alloc::Unbound).  Unbound has no
// AllocClassTag analogue; instantiating the lift with Unbound
// fires a clean static_assert.

#include <crucible/effects/ExecCtx.h>
#include <crucible/safety/AllocClass.h>
#include <crucible/safety/HotPath.h>
#include <crucible/safety/ResidencyHeat.h>

namespace crucible::effects {

// ── HotPathFromCtx ──────────────────────────────────────────────────

template <IsExecCtx Ctx, class T>
using HotPathFromCtx = ::crucible::safety::HotPath<
    to_hot_path_tier_v<hot_path_tier_of_t<Ctx>>, T>;

// ── AllocClassFromCtx (requires bound alloc) ────────────────────────
//
// ctx_alloc::Unbound has no analogue in algebra::lattices::
// AllocClassTag.  We gate the alias with a trampoline that
// static_asserts on Unbound — the user gets a clean diagnostic
// rather than the compile-error cascade from instantiating
// to_alloc_class_tag<ctx_alloc::Unbound> (the primary template is
// undefined).

namespace detail {
template <class AllocT>
inline constexpr bool is_unbound_alloc_v =
    std::is_same_v<AllocT, ctx_alloc::Unbound>;

template <IsExecCtx Ctx, class T>
struct alloc_class_from_ctx {
    static_assert(!is_unbound_alloc_v<alloc_class_of_t<Ctx>>,
        "AllocClassFromCtx<Ctx, T> requires Ctx::alloc_class to be "
        "bound (one of ctx_alloc::{Stack, Arena, Pool, Heap, "
        "HugePage}).  ctx_alloc::Unbound has no wrapper analogue.");
    using type = ::crucible::safety::AllocClass<
        to_alloc_class_tag_v<alloc_class_of_t<Ctx>>, T>;
};
}  // namespace detail

template <IsExecCtx Ctx, class T>
using AllocClassFromCtx = typename detail::alloc_class_from_ctx<Ctx, T>::type;

// ── ResidencyHeatFromCtx ────────────────────────────────────────────

template <IsExecCtx Ctx, class T>
using ResidencyHeatFromCtx = ::crucible::safety::ResidencyHeat<
    to_residency_heat_tag_v<residency_of_t<Ctx>>, T>;

// ── Self-test block ─────────────────────────────────────────────────
namespace detail::ctx_wrapper_lift_self_test {

namespace lat = ::crucible::algebra::lattices;
namespace saf = ::crucible::safety;

// ── HotPathFromCtx pinning ──────────────────────────────────────────

// HotFgCtx (Hot tier) → HotPath<Hot, …>.
static_assert(std::is_same_v<HotPathFromCtx<HotFgCtx, int>,
                              saf::HotPath<lat::HotPathTier::Hot, int>>);

// BgDrainCtx / BgCompileCtx (Warm tier) → HotPath<Warm, …>.
static_assert(std::is_same_v<HotPathFromCtx<BgDrainCtx, int>,
                              saf::HotPath<lat::HotPathTier::Warm, int>>);
static_assert(std::is_same_v<HotPathFromCtx<BgCompileCtx, double>,
                              saf::HotPath<lat::HotPathTier::Warm, double>>);

// ColdInitCtx / TestRunnerCtx (Cold tier) → HotPath<Cold, …>.
static_assert(std::is_same_v<HotPathFromCtx<ColdInitCtx, void*>,
                              saf::HotPath<lat::HotPathTier::Cold, void*>>);
static_assert(std::is_same_v<HotPathFromCtx<TestRunnerCtx, char>,
                              saf::HotPath<lat::HotPathTier::Cold, char>>);

// EBO collapse preserved.
static_assert(sizeof(HotPathFromCtx<HotFgCtx, int>) == sizeof(int));

// ── AllocClassFromCtx pinning ───────────────────────────────────────

static_assert(std::is_same_v<AllocClassFromCtx<HotFgCtx, int*>,
                              saf::AllocClass<lat::AllocClassTag::Stack, int*>>);
static_assert(std::is_same_v<AllocClassFromCtx<BgDrainCtx, void*>,
                              saf::AllocClass<lat::AllocClassTag::Arena, void*>>);
static_assert(std::is_same_v<AllocClassFromCtx<ColdInitCtx, void*>,
                              saf::AllocClass<lat::AllocClassTag::Heap, void*>>);
static_assert(std::is_same_v<AllocClassFromCtx<TestRunnerCtx, void*>,
                              saf::AllocClass<lat::AllocClassTag::Heap, void*>>);

static_assert(sizeof(AllocClassFromCtx<BgDrainCtx, void*>) == sizeof(void*));

// ── ResidencyHeatFromCtx pinning ────────────────────────────────────
//
// 4-3 collapse: L1 → Hot, L2 → Hot, L3 → Warm, DRAM → Cold.

static_assert(std::is_same_v<ResidencyHeatFromCtx<HotFgCtx, int>,
                              saf::ResidencyHeat<lat::ResidencyHeatTag::Hot, int>>);
static_assert(std::is_same_v<ResidencyHeatFromCtx<BgDrainCtx, int>,
                              saf::ResidencyHeat<lat::ResidencyHeatTag::Hot, int>>);
static_assert(std::is_same_v<ResidencyHeatFromCtx<ColdInitCtx, int>,
                              saf::ResidencyHeat<lat::ResidencyHeatTag::Cold, int>>);

static_assert(sizeof(ResidencyHeatFromCtx<HotFgCtx, int>) == sizeof(int));

}  // namespace detail::ctx_wrapper_lift_self_test

// ── Runtime smoke test ──────────────────────────────────────────────

[[gnu::cold]] inline void runtime_smoke_test_ctx_wrapper_lift() noexcept {
    // Instantiate each lifted wrapper at runtime to confirm the
    // alias resolves to a constructible type.
    HotPathFromCtx<HotFgCtx, int>             hp_hot{42};
    HotPathFromCtx<BgDrainCtx, int>           hp_warm{7};
    HotPathFromCtx<ColdInitCtx, int>          hp_cold{0};

    AllocClassFromCtx<HotFgCtx, int*>         ac_stack{nullptr};
    AllocClassFromCtx<BgDrainCtx, void*>      ac_arena{nullptr};
    AllocClassFromCtx<ColdInitCtx, void*>     ac_heap{nullptr};

    ResidencyHeatFromCtx<HotFgCtx, int>       rh_hot{1};
    ResidencyHeatFromCtx<BgDrainCtx, int>     rh_l2{2};
    ResidencyHeatFromCtx<ColdInitCtx, int>    rh_dram{3};

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
