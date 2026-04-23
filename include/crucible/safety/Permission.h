#pragma once

// ── crucible::safety::Permission<Tag> — CSL frame-rule primitive ─────
//
// Phantom-typed move-only token encoding "the bearer holds the
// exclusive right to mutate the memory region named by Tag".  The
// type system mechanizes Concurrent Separation Logic's frame rule
// (O'Hearn 2007): if two threads each hold a Permission for disjoint
// Tags, they cannot conflict — the optimizer can prove it AND the
// human reviewer gets a one-line check at every handoff.
//
//   Axiom coverage: BorrowSafe, ThreadSafe, MemSafe (code_guide §II).
//   Runtime cost:   zero — sizeof(Permission<Tag>) == 1
//                   (empty class minimum); collapses to 0 via
//                   [[no_unique_address]] in containing structs.
//
// ─── How linearity encodes the frame rule ───────────────────────────
//
// CSL's separating conjunction `*` says "P and Q hold over disjoint
// regions".  In C++, "disjoint" is captured by:
//
//   Linear (move-only)  → "exactly one owner at any moment"  (the * of CSL)
//   Tag                 → "which region this permission covers" (the labels)
//   move                → "ownership transfers"                  (frame's R)
//   permission_split    → "splitting a region into disjoint parts" (P * Q ⊢ P, Q)
//   permission_combine  → "merging two parts back"                (P, Q ⊢ P * Q)
//
// A thread that wants to mutate region R must hold Permission<R>.  By
// linearity, no two simultaneously-live Permission<R> values can
// exist; by transitivity, no two threads can mutate R simultaneously.
// Compile-time enforcement of separation logic.
//
// ─── Discipline (what the framework enforces vs what it doesn't) ───
//
// ENFORCED at compile time:
//   * Move-only (no two values for the same Tag exist)
//   * Tag identity at every handoff site (type system, no implicit conv)
//   * Declared splits via splits_into trait (compile error if undeclared)
//   * Construction only via grep-discoverable friend factories
//
// NOT enforced:
//   * That Tag actually corresponds to the memory you're claiming
//     (you must wire the Tag into your handle types — see
//     concurrent/Queue.h's per-Kind tag trees for the canonical pattern)
//   * That the body of code holding a permission only touches that
//     region (no flow-sensitive alias analysis in C++)
//
// For full proof discipline, hold the Permission inside a handle
// (Pinned, RAII) so the handle's methods become the gated operations
// — see Queue<T, Kind>::ProducerHandle in concurrent/Queue.h.
//
// ─── Usage pattern ──────────────────────────────────────────────────
//
//   // 1. Define the region-tag tree (declarative manifest):
//   namespace ring_tags {
//       struct Whole {};
//       struct Producer {};
//       struct Consumer {};
//   }
//   namespace crucible::safety {
//       template <> struct splits_into<ring_tags::Whole,
//                                      ring_tags::Producer,
//                                      ring_tags::Consumer>
//                    : std::true_type {};
//   }
//
//   // 2. Mint root at startup (single call per Whole tag):
//   auto whole = permission_root_mint<ring_tags::Whole>();
//
//   // 3. Split into disjoint subregions:
//   auto [prod, cons] = permission_split<ring_tags::Producer,
//                                        ring_tags::Consumer>(
//                                                std::move(whole));
//
//   // 4. Hand off across threads via std::jthread move (or use
//   //    permission_fork for structured fork-join — see
//   //    safety/PermissionFork.h):
//   std::jthread producer_thread{[p = std::move(prod)](auto) mutable {
//       /* p is the only Permission<Producer> in the program */
//   }};
//
// ─── Discipline (anti-patterns to reject on review) ─────────────────
//
// 1. NEVER store a `Permission<Tag>` in a long-lived data structure
//    that is shared between threads.  The struct may be aliased and
//    the type system can't see it; defeats linearity.
// 2. Mint each root tag exactly once.  No machinery enforces this —
//    the rule is grep-discoverable (`permission_root_mint<` is the
//    only construction call).
// 3. Tag tree splits_into specializations belong in the SAME
//    translation unit as the tag definitions.  Reviewers should see
//    the whole region tree in one place.

#include <crucible/Platform.h>

#include <concepts>
#include <tuple>
#include <type_traits>
#include <utility>

namespace crucible::safety {

// ── splits_into trait ────────────────────────────────────────────────
//
// Declarative manifest of valid splits.  Specialize per region tree:
//
//   template <> struct splits_into<Whole, Producer, Consumer>
//                : std::true_type {};
//
// permission_split<L, R>(Permission<In>&&) requires
// splits_into<In, L, R>::value.  Same trait constrains
// permission_combine<In>(Permission<L>&&, Permission<R>&&).

template <typename Parent, typename L, typename R>
struct splits_into : std::false_type {};

template <typename Parent, typename L, typename R>
inline constexpr bool splits_into_v = splits_into<Parent, L, R>::value;

// ── splits_into_pack trait ───────────────────────────────────────────
//
// N-ary variant — declares Parent splits into Children...  Used for
// sharded grids (M producers × N consumers) and any other case
// where a region naturally decomposes into more than two disjoint parts.

template <typename Parent, typename... Children>
struct splits_into_pack : std::false_type {};

template <typename Parent, typename... Children>
inline constexpr bool splits_into_pack_v =
    splits_into_pack<Parent, Children...>::value;

// ── Permission<Tag> ──────────────────────────────────────────────────
//
// Phantom-typed linear token.  Tag is never instantiated; only its
// identity matters.  The token itself carries no data — it is proof,
// not payload.

template <typename Tag>
class [[nodiscard]] Permission {
    // Empty — sizeof is the empty-class minimum (1 byte).  Marking
    // the field [[no_unique_address]] in containing types collapses
    // it to 0 bytes via EBO.

    // Private default constructor.  Only the friended factories
    // construct Permissions; every construction call site is
    // discoverable via grep on the factory names.
    constexpr Permission() noexcept = default;

    // Friend access list — kept short on purpose.  Each addition is
    // a new way to mint a Permission and demands review.

    template <typename T>
    friend constexpr Permission<T> permission_root_mint() noexcept;

    template <typename L, typename R, typename In>
    friend constexpr std::pair<Permission<L>, Permission<R>>
    permission_split(Permission<In>&&) noexcept;

    template <typename In, typename L, typename R>
    friend constexpr Permission<In>
    permission_combine(Permission<L>&&, Permission<R>&&) noexcept;

    template <typename... Children, typename In>
    friend constexpr std::tuple<Permission<Children>...>
    permission_split_n(Permission<In>&&) noexcept;

    // PermissionFork rebuilds the parent Permission after children
    // have been consumed by their callables.  See safety/PermissionFork.h.
    template <typename T>
    friend constexpr Permission<T> permission_fork_rebuild_() noexcept;

public:
    using tag_type = Tag;

    // Linearity: copy deleted with reason; move defaulted (the
    // moved-from Permission is empty and inert).  -Werror=use-after-move
    // catches double-consume.
    Permission(const Permission&)
        = delete("Permission<Tag>: linear — duplicating creates two simultaneous owners of the same region, breaking CSL's frame rule.  Use std::move to transfer.");
    Permission& operator=(const Permission&)
        = delete("Permission<Tag>: linear — assignment would overwrite an existing permission token.");
    constexpr Permission(Permission&&) noexcept            = default;
    constexpr Permission& operator=(Permission&&) noexcept = default;
    ~Permission() = default;
};

// ── Free function: explicit drop ────────────────────────────────────
//
// Equivalent to letting the rvalue go out of scope, but communicates
// "I am intentionally discarding this permission" at the call site.
// The corresponding region is unowned forever after this — a fresh
// permission cannot be re-minted at the same Tag (or rather, can only
// be done by re-calling permission_root_mint, which is discouraged
// outside startup).

template <typename Tag>
constexpr void permission_drop(Permission<Tag>&&) noexcept {
    // The rvalue parameter destructs at end of scope.
}

// ── Factories ────────────────────────────────────────────────────────

// Root mint.  Call exactly once per Tag at startup.  No machinery
// detects double-mint; the call site is grep-discoverable
// (`permission_root_mint<` matches every minting site).
//
// Cost: returns a 1-byte empty token.  Inlined to a no-op.
template <typename Tag>
[[nodiscard]] constexpr Permission<Tag> permission_root_mint() noexcept {
    return Permission<Tag>{};
}

// Binary split.  Returns disjoint Permission<L> and Permission<R>;
// the input Permission<In> is consumed.  Compile error if
// splits_into<In, L, R> hasn't been specialized true.
template <typename L, typename R, typename In>
[[nodiscard]] constexpr std::pair<Permission<L>, Permission<R>>
permission_split(Permission<In>&& parent) noexcept {
    static_assert(splits_into_v<In, L, R>,
                  "permission_split<L, R>(Permission<In>&&) requires "
                  "splits_into<In, L, R>::value to be specialized true.  "
                  "Declare the split in the same TU that defines the tags.");
    (void)parent;
    return std::pair<Permission<L>, Permission<R>>{
        Permission<L>{}, Permission<R>{}
    };
}

// Inverse: combine two disjoint permissions back into the parent.
// Symmetric to split — same splits_into constraint.
template <typename In, typename L, typename R>
[[nodiscard]] constexpr Permission<In>
permission_combine(Permission<L>&& left, Permission<R>&& right) noexcept {
    static_assert(splits_into_v<In, L, R>,
                  "permission_combine<In>(Permission<L>&&, Permission<R>&&) "
                  "requires splits_into<In, L, R>::value true.");
    (void)left;
    (void)right;
    return Permission<In>{};
}

// N-ary split.  Returns a tuple of disjoint Permissions — one per
// Child tag.  Used for sharded grids (one Permission per shard) and
// the structured-concurrency fork primitive (PermissionFork.h).
template <typename... Children, typename In>
[[nodiscard]] constexpr std::tuple<Permission<Children>...>
permission_split_n(Permission<In>&& parent) noexcept {
    static_assert(splits_into_pack_v<In, Children...>,
                  "permission_split_n<Children...>(Permission<In>&&) "
                  "requires splits_into_pack<In, Children...>::value true.");
    (void)parent;
    return std::tuple<Permission<Children>...>{
        Permission<Children>{}...
    };
}

// ── Internal: rebuild helper for PermissionFork ──────────────────────
//
// Friend of Permission so safety/PermissionFork.h can reconstruct the
// parent Permission after all children have been consumed by their
// callables (and the jthreads have joined).  Per the discipline note
// in the header doc, the rebuild is justified by the structural-join
// invariant: every child callable consumed its child Permission inside
// its body; the jthread destructor in PermissionFork joined the worker
// before returning; no Permission<Child_i> for any i remains live; so
// the parent region is again exclusively available to the joining
// scope and a fresh Permission<Parent> can be issued.
//
// When child handles OWN their Permission via [[no_unique_address]]
// (the canonical pattern in concurrent/Queue.h), the consumption is
// structural: the handle's destructor at the end of the lambda body
// destructs the embedded Permission.
//
// NOT a public API; users must go through permission_fork.
template <typename T>
[[nodiscard]] constexpr Permission<T> permission_fork_rebuild_() noexcept {
    return Permission<T>{};
}

// ── Zero-cost guarantees ──────────────────────────────────────────────

namespace detail {
    struct seplog_test_tag {};
    struct seplog_test_left {};
    struct seplog_test_right {};
}

// Permission is a 1-byte empty class; not movable across translation
// units without copies but the move constructor is a noop.
static_assert(sizeof(Permission<detail::seplog_test_tag>) == 1,
              "Permission<Tag> must be a 1-byte empty class");
static_assert(std::is_trivially_destructible_v<Permission<detail::seplog_test_tag>>,
              "Permission<Tag> destructor must be trivial");
static_assert(!std::is_copy_constructible_v<Permission<detail::seplog_test_tag>>,
              "Permission<Tag> must NOT be copy-constructible (linear)");
static_assert(!std::is_copy_assignable_v<Permission<detail::seplog_test_tag>>,
              "Permission<Tag> must NOT be copy-assignable (linear)");
static_assert(std::is_move_constructible_v<Permission<detail::seplog_test_tag>>,
              "Permission<Tag> must be move-constructible (handoff)");
static_assert(std::is_nothrow_move_constructible_v<Permission<detail::seplog_test_tag>>,
              "Permission<Tag> moves must be noexcept");

}  // namespace crucible::safety
