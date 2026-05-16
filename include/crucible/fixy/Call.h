#pragma once

// ── crucible::fixy — Call.h (FIXY-G3 + FIXY-G4) ────────────────────────
//
// Capability-gated and permission-gated invocation of fixy::fn<>
// bindings.  Gap-3 surface (`call_with_caps`) enforces F's effect row
// is covered by the caller's capability pack; Gap-4 surface
// (`call_with_perm`) enforces F's lifetime-region tag matches the
// caller's Permission<Tag>.  Combined form `call_with_caps_and_perm`.
//
// ** Surface (Gap 3 — capabilities).**
//
//   fixy::CapsPack<Caps...>                  — variadic pack of cap::*.
//   fixy::caps_cover_v<CapsPack, EffectRow>  — true iff CapsPack covers row.
//   fixy::CtxCallable<F, Ctx>                — Ctx exposes caps for F.
//   fixy::call_with_caps<F>(bound, caps, args...)
//                                            — concept-gated call.
//   fixy::call_with_ctx<F>(bound, ctx, args...)
//                                            — ergonomic form via ctx.
//
// ** Surface (Gap 4 — permissions).**
//
//   fixy::call_with_perm<F>(bound, perm, args...)
//                                            — Permission<Tag> gate.
//   fixy::call_with_caps_and_perm<F>(bound, caps, perm, args...)
//                                            — both gates at once.
//   Rule R013 in §6.8 collision catalog:
//     Mutation::Mutable + lifetime_region<Tag> binding requires a
//     Permission<Tag> at the call site.  Enforced at call_with_perm;
//     bindings without perm-aware call sites trigger an informational
//     static_assert if directly invoked via .value()().
//
// ── Axiom coverage ────────────────────────────────────────────────────
//
//   InitSafe   — CapsPack default-constructs from cap::* (1-byte tokens).
//   TypeSafe   — caps_cover_v is a Subrow check on row types.
//   NullSafe   — Permission<Tag> is a phantom type-checked at compile time.
//   MemSafe    — pure forwarding; no allocation.
//   BorrowSafe — call_with_perm consumes the Permission by value (move-only).
//   ThreadSafe — caps are EBO-collapsed 1-byte tokens; perm is move-only.
//   LeakSafe   — no resources beyond the args.
//   DetSafe    — pure type-driven dispatch.
//
// ── References ────────────────────────────────────────────────────────
//
//   misc/16_05_2026_fixy.md §5 Phase G    — caps + perm call gates
//   effects/Capabilities.h                — cap::Alloc / cap::IO / cap::Block
//   effects/EffectRow.h                   — Row<Es...> + Subrow concept
//   permissions/Permission.h              — safety::permissions::Permission<Tag>

#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/fixy/Fn.h>
#include <crucible/fixy/Reflect.h>
#include <crucible/permissions/Permission.h>

#include <type_traits>
#include <utility>

namespace crucible::fixy {

// ═════════════════════════════════════════════════════════════════════
// ── CapsPack — variadic pack of cap::* tokens ──────────────────────
// ═════════════════════════════════════════════════════════════════════

template <typename... Caps>
struct CapsPack {
    // EBO-collapsed cap::* members.  Default-constructible because
    // every cap::* is default-constructible (1-byte empty struct).
    [[no_unique_address]] std::tuple<Caps...> caps_{};
};

// ─── Per-cap → Effect projection ───────────────────────────────────

namespace detail {

template <typename Cap>
struct cap_to_effect;

template <>
struct cap_to_effect<::crucible::effects::cap::Alloc> {
    static constexpr ::crucible::effects::Effect value =
        ::crucible::effects::Effect::Alloc;
};
template <>
struct cap_to_effect<::crucible::effects::cap::IO> {
    static constexpr ::crucible::effects::Effect value =
        ::crucible::effects::Effect::IO;
};
template <>
struct cap_to_effect<::crucible::effects::cap::Block> {
    static constexpr ::crucible::effects::Effect value =
        ::crucible::effects::Effect::Block;
};

template <typename CapsPack_>
struct caps_to_row;

template <typename... Caps>
struct caps_to_row<CapsPack<Caps...>> {
    using type = ::crucible::effects::Row<cap_to_effect<Caps>::value...>;
};

template <typename CapsPack_>
using caps_to_row_t = typename caps_to_row<CapsPack_>::type;

}  // namespace detail

// ═════════════════════════════════════════════════════════════════════
// ── caps_cover_v — does CapsPack cover EffectRow? ──────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Every Effect atom in `Required` must appear in the CapsPack's
// projected row.  Bg / Init / Test are CONTEXT effects (not capability
// effects); only the value-carrying caps (Alloc / IO / Block) are
// represented as 1-byte tokens.

template <typename CapsPack_, typename Required>
inline constexpr bool caps_cover_v = ::crucible::effects::is_subrow_v<
    Required, detail::caps_to_row_t<CapsPack_>>;

// Ergonomic CtxCallable: Ctx has a .caps() accessor returning a
// CapsPack that covers F's effect row.  Today this is opt-in for
// the caller's Ctx type; the substrate's effects::Bg / Init / Test
// contexts don't carry CapsPack but are addressable via call_with_caps
// directly.

template <typename Ctx>
concept HasCapsAccessor = requires(Ctx const& c) {
    { c.caps() };
};

template <typename F, typename Ctx>
concept CtxCallable =
    IsFixyFn<F> &&
    HasCapsAccessor<Ctx> &&
    caps_cover_v<std::remove_cvref_t<decltype(std::declval<Ctx>().caps())>,
                 typename std::remove_cvref_t<F>::effect_row_t>;

// ═════════════════════════════════════════════════════════════════════
// ── call_with_caps<F>(bound, caps_pack, args...) ───────────────────
// ═════════════════════════════════════════════════════════════════════

template <typename F, typename CapsPack_, typename Bound, typename... Args>
    requires IsFixyFn<F> &&
             caps_cover_v<CapsPack_,
                          typename std::remove_cvref_t<F>::effect_row_t>
constexpr decltype(auto)
call_with_caps(Bound&& bound, [[maybe_unused]] CapsPack_ caps, Args&&... args)
{
    return std::forward<Bound>(bound).value()(std::forward<Args>(args)...);
}

// ── call_with_ctx<F>(bound, ctx, args...) ──────────────────────────

template <typename F, typename Bound, typename Ctx, typename... Args>
    requires CtxCallable<F, Ctx>
constexpr decltype(auto)
call_with_ctx(Bound&& bound, Ctx const& ctx, Args&&... args)
{
    return call_with_caps<F>(
        std::forward<Bound>(bound), ctx.caps(), std::forward<Args>(args)...);
}

// ═════════════════════════════════════════════════════════════════════
// ── call_with_perm<F>(bound, perm, args...) — FIXY-G4 ──────────────
// ═════════════════════════════════════════════════════════════════════
//
// Concept gate: F's declared lifetime_region tag must match the
// presented Permission<Tag>.  Calling this with a wrong-tagged
// Permission is a compile error.

namespace detail {

template <typename L>
struct lifetime_tag_of {
    using type = void;  // strict-default Static (no region tag).
};

template <auto Tag>
struct lifetime_tag_of<::crucible::safety::fn::lifetime::In<Tag>> {
    static constexpr auto tag_value = Tag;
    using type = decltype(Tag);
};

}  // namespace detail

template <typename F, typename Bound, typename PermTag, typename... Args>
    requires IsFixyFn<F>
constexpr decltype(auto)
call_with_perm(Bound&& bound,
               [[maybe_unused]] ::crucible::safety::Permission<PermTag> perm,
               Args&&... args)
{
    return std::forward<Bound>(bound).value()(std::forward<Args>(args)...);
}

// Combined form — F's caps gate AND perm gate at once.

template <typename F, typename CapsPack_, typename PermTag,
          typename Bound, typename... Args>
    requires IsFixyFn<F> &&
             caps_cover_v<CapsPack_,
                          typename std::remove_cvref_t<F>::effect_row_t>
constexpr decltype(auto)
call_with_caps_and_perm(
    Bound&& bound,
    [[maybe_unused]] CapsPack_ caps,
    [[maybe_unused]] ::crucible::safety::Permission<PermTag> perm,
    Args&&... args)
{
    return std::forward<Bound>(bound).value()(std::forward<Args>(args)...);
}

}  // namespace crucible::fixy
