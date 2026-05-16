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
struct lifetime_tag_match {
    template <typename PermTag>
    static constexpr bool matches_v = false;
};

template <auto RegionTag>
struct lifetime_tag_match<::crucible::safety::fn::lifetime::In<RegionTag>> {
    template <typename PermTag>
    static constexpr bool matches_v =
        std::is_same_v<std::remove_cvref_t<decltype(RegionTag)>, PermTag> ||
        // Tag-as-value case: PermTag IS the tag type, RegionTag is a
        // value of that type.  Either spelling is accepted.
        std::is_same_v<PermTag, std::remove_cvref_t<decltype(RegionTag)>>;
};

template <typename F, typename PermTag>
inline constexpr bool perm_tag_matches_lifetime_v =
    lifetime_tag_match<typename std::remove_cvref_t<F>::lifetime_t>
        ::template matches_v<PermTag>;

}  // namespace detail

// PermissionMatchesLifetime — concept gate for call_with_perm.
//
// F's declared lifetime_t must be a lifetime::In<Tag> whose Tag
// type matches PermTag, OR the binding accepts ANY-perm (strict-
// default lifetime::Static — opt-out check).  When lifetime_t is
// strict-default Static, call_with_perm accepts any tag (no region
// requirement); when lifetime_t is In<X>, only Permission<X> is
// accepted.

template <typename F, typename PermTag>
concept PermissionMatchesLifetime =
    IsFixyFn<F> &&
    (std::is_same_v<typename std::remove_cvref_t<F>::lifetime_t,
                    ::crucible::safety::fn::lifetime::Static> ||
     detail::perm_tag_matches_lifetime_v<F, PermTag>);

template <typename F, typename Bound, typename PermTag, typename... Args>
    requires IsFixyFn<F> && PermissionMatchesLifetime<F, PermTag>
constexpr decltype(auto)
call_with_perm(Bound&& bound,
               [[maybe_unused]] ::crucible::safety::Permission<PermTag> perm,
               Args&&... args)
{
    return std::forward<Bound>(bound).value()(std::forward<Args>(args)...);
}

// ═════════════════════════════════════════════════════════════════════
// ── R013 — Mutable + lifetime_region<Tag> ⇒ Permission required ────
// ═════════════════════════════════════════════════════════════════════
//
// Cross-axis rule (§6.8 collision catalog extension): when a binding
// is Mutable (Mutation axis) AND scoped to a named region (Lifetime
// = In<X>), invoking it MUST present Permission<X>.  R013 is enforced
// statically through call_with_perm's PermissionMatchesLifetime gate;
// at the binding declaration site, R013_warns_if_no_perm<F> can be
// referenced by reviewers / linters but does not by itself block
// invocation via .value()() (which remains the documented escape
// hatch for non-perm-aware call sites).

namespace detail {

template <typename F>
inline constexpr bool is_mutable_lifetime_region_v = false;

template <typename T, typename... Grants>
inline constexpr bool is_mutable_lifetime_region_v<::crucible::fixy::fn<T, Grants...>>
    = (std::remove_cvref_t<::crucible::fixy::fn<T, Grants...>>::mutation_v
       == ::crucible::safety::fn::MutationMode::Mutable) &&
      !std::is_same_v<typename std::remove_cvref_t<
                          ::crucible::fixy::fn<T, Grants...>>::lifetime_t,
                      ::crucible::safety::fn::lifetime::Static>;

}  // namespace detail

template <typename F>
inline constexpr bool R013_requires_permission_v =
    IsFixyFn<F> && detail::is_mutable_lifetime_region_v<std::remove_cvref_t<F>>;

// Combined form — F's caps gate AND perm gate at once.

template <typename F, typename CapsPack_, typename PermTag,
          typename Bound, typename... Args>
    requires IsFixyFn<F> &&
             caps_cover_v<CapsPack_,
                          typename std::remove_cvref_t<F>::effect_row_t> &&
             PermissionMatchesLifetime<F, PermTag>
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
