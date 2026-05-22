#pragma once

// ── crucible::fixy::ctrl::throws — control-flow grant: callable may throw ─
//
// FIXY-V-087 forward-pioneer of the V-244 `fixy/grant/Ctrl.h` surface
// (Agent 10 §3c — throws/abort/longjmp/exit/coroutine grant family).
//
// V-244 will ship the full Ctrl grant family under `fixy::grant::ctrl::`
// once the V-238 ControlFlow DimensionAxis lands; until then, V-087
// ships JUST `throws` under `fixy::ctrl::` — CR-09 (only `Grant.h` may
// open `crucible::fixy::grant::`) keeps us out of the centralized
// grant namespace, but the tag still inherits `grant::grant_base` so it
// participates in `IsGrantTag`.  When V-238 + V-244 materialize the
// full ControlFlow axis, this tag re-homes via a thin `using` alias.
//
// ── Why this tag is load-bearing for mint_permission_fork ─────────────
//
// `mint_permission_fork` (CSL parallel rule, RAII fork-join over
// `std::jthread`) admits Callables that satisfy
// `is_nothrow_invocable_v<Callable, Permission<Child>, Ctx>`.  That is
// a SYNTACTIC check — a callable declared `noexcept` may LIE: under
// `-fno-exceptions` the throw is rewritten to `std::abort`, under
// `-fexceptions` it propagates to the noexcept boundary and calls
// `std::terminate`.  Either way the structured-concurrency join is
// torn through — Permission rebuild for the parent never runs, child
// Permission lifetimes are stranded, and the type-system invariant
// that "after fork, parent is exclusive again" is lost.
//
// `throws` is the explicit, type-level marker for "this callable WILL
// transit through a throw at some point".  `mint_permission_fork`'s
// body adds the static_assert:
//
//   static_assert(!(type_tree_contains_throws_v<Callables> || ...),
//                 "mint_permission_fork: throws not permitted...");
//
// — so any Callable whose TYPE TREE carries a `ctrl::throws` grant
// (e.g., via a `fixy::fn<T, ..., ctrl::throws, ...>` annotation, or a
// named callable wrapper whose template args declare it) is rejected
// at the fork site BEFORE the noexcept-invocable check.  Lambda
// closures (which have no introspectable template args) pass the
// type-tree test and rely on the noexcept-invocable rail — that's
// fine; the load-bearing case is NAMED callable wrappers that lie via
// noexcept-decl while carrying an explicit throw permit.
//
// ── Axiom coverage ────────────────────────────────────────────────────
//
//   TypeSafe   — `throws` is a final empty struct, not interconvertible
//                with any other tag type.
//   ThreadSafe — load-bearing on the new `mint_permission_fork`
//                static_assert; a Callable with `throws` in its type
//                tree reddens at the fork site.
//   InitSafe   — empty `final` tag, no NSDMI hazard, no state.
//   MemSafe    — type-level only, zero allocation, zero lifetime.
//   NullSafe / BorrowSafe / LeakSafe / DetSafe — not applicable
//                (zero runtime cost, pure type-system surface).
//
// ── Runtime cost ──────────────────────────────────────────────────────
//
// Zero.  `throws` is an empty `final` struct (`sizeof == 1` only because
// the C++ object model requires distinct addresses for distinct
// objects; EBO-collapses to 0 bytes in any composition).
//
// ── References ────────────────────────────────────────────────────────
//
//   FIXY-V-244 — `fixy/grant/Ctrl.h` full Ctrl grant family (deferred).
//   FIXY-V-238 — `DimensionAxis::ControlFlow` enumerator + lattice
//                scaffolding (deferred).
//   FIXY-V-086 — `BackgroundThread::RegionReadyCallback::Fn += noexcept`
//                (the assignment-conversion sibling discipline).
//   CLAUDE.md §VI ThreadSafe axiom.
//   permissions/PermissionFork.h fixy-A1-010 — `-fno-exceptions` /
//                `-fexceptions` mode-independent noexcept honoring
//                of `mint_permission_fork`.

#include <crucible/fixy/Grant.h>

#include <tuple>
#include <type_traits>

namespace crucible::fixy::ctrl {

// ─── `throws` — control-flow grant: callable may throw ──────────────
//
// Inherits `crucible::fixy::grant::grant_base` so it participates in
// the `IsGrantTag` concept.  `final` per CR-09 partner rule (caller
// cannot extend tags to fake-pass concept checks).
struct throws final : ::crucible::fixy::grant::grant_base {};

// ─── type_tree_contains<Needle, Haystack> — recursive type-tree search ───
//
// Returns true if `Haystack` contains `Needle` anywhere in its template
// instantiation tree, with cv/reference decay applied at every node.
//
// Recursion is over `template <typename...> class Tmpl<Args...>` only —
// types with non-type template params (`std::array<T,N>`, integer-
// indexed packs) fall through to the primary template with no match.
// That is an accepted false-negative: V-087 needs to catch NAMED
// callable wrappers whose template args carry `ctrl::throws`; lambdas
// and array-shaped carriers fall back to the noexcept-invocable rail.

namespace detail {

template <typename Needle, typename Haystack>
inline constexpr bool type_tree_match_v =
    std::is_same_v<std::remove_cvref_t<Haystack>, Needle>;

template <typename Needle, typename Haystack>
struct type_tree_contains
    : std::bool_constant<type_tree_match_v<Needle, Haystack>> {};

// Recursive specialization: `Haystack` is itself a template instantiation
// `Tmpl<Args...>`.  Fold over Args.
template <typename Needle,
          template <typename...> class Tmpl,
          typename... Args>
struct type_tree_contains<Needle, Tmpl<Args...>>
    : std::bool_constant<
          type_tree_match_v<Needle, Tmpl<Args...>>
          || (type_tree_contains<Needle, Args>::value || ...)> {};

}  // namespace detail

template <typename Needle, typename Haystack>
inline constexpr bool type_tree_contains_v =
    detail::type_tree_contains<Needle, Haystack>::value;

// ─── Convenience: detect `throws` in arbitrary type ───────────────────
template <typename T>
inline constexpr bool type_tree_contains_throws_v =
    type_tree_contains_v<throws, T>;

// ═════════════════════════════════════════════════════════════════════
// ── Surface integrity sentinels ──────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail::ctrl_self_test {

// ─── (1) Tag identity + structural shape ─────────────────────────────
static_assert(::crucible::fixy::grant::IsGrantTag<throws>);
static_assert(std::is_empty_v<throws>);
static_assert(std::is_final_v<throws>);
static_assert(std::is_base_of_v<::crucible::fixy::grant::grant_base, throws>);
static_assert(sizeof(throws) == 1);

// `throws` is the only sub-tag V-087 ships; V-244 will add the rest.

// ─── (2) Non-grants are concept-negative ─────────────────────────────
struct unrelated_tag {};
static_assert(!::crucible::fixy::grant::IsGrantTag<int>);
static_assert(!::crucible::fixy::grant::IsGrantTag<unrelated_tag>);

// ─── (3) type_tree_contains witnesses — positive ─────────────────────
static_assert(type_tree_contains_v<throws, throws>);
static_assert(type_tree_contains_v<throws, throws const>);
static_assert(type_tree_contains_v<throws, throws&>);
static_assert(type_tree_contains_v<throws, throws const&>);
static_assert(type_tree_contains_v<throws, throws volatile>);
static_assert(type_tree_contains_v<throws, std::tuple<throws>>);
static_assert(type_tree_contains_v<throws, std::tuple<int, throws>>);
static_assert(type_tree_contains_v<throws, std::tuple<int, throws const&>>);
static_assert(type_tree_contains_v<throws,
              std::tuple<int, std::tuple<throws, double>>>);
static_assert(type_tree_contains_v<throws,
              std::tuple<int, std::tuple<unrelated_tag, std::tuple<throws>>>>);

// ─── (4) type_tree_contains witnesses — negative ─────────────────────
static_assert(!type_tree_contains_v<throws, int>);
static_assert(!type_tree_contains_v<throws, int const&>);
static_assert(!type_tree_contains_v<throws, unrelated_tag>);
static_assert(!type_tree_contains_v<throws, std::tuple<int, double>>);
static_assert(!type_tree_contains_v<throws,
              std::tuple<int, std::tuple<unrelated_tag, double>>>);

// ─── (5) Convenience alias agrees with primary trait ─────────────────
static_assert(type_tree_contains_throws_v<throws>);
static_assert(type_tree_contains_throws_v<std::tuple<int, throws>>);
static_assert(!type_tree_contains_throws_v<int>);
static_assert(!type_tree_contains_throws_v<std::tuple<int, double>>);

}  // namespace detail::ctrl_self_test

}  // namespace crucible::fixy::ctrl
