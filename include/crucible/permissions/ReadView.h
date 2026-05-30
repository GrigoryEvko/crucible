#pragma once

// ── crucible::safety::ReadView<Tag> ──────────────────────────────────
//
// THE third CSL permission flavor — lifetime-bound read borrow.
//
//   Permission<Tag>           — exclusive write (linear, move-only)
//   SharedPermission<Tag>+Pool — multi-reader/writer (atomic refcount)
//   ReadView<Tag>             — function-call-scoped read borrow
//
//   sizeof(ReadView<Tag>) == 1     (empty class; EBO-collapsible to 0)
//   Runtime cost              = zero
//
// ─── When ReadView vs SharedPermission ──────────────────────────────
//
//   Use ReadView when:
//     * The borrow is SCOPED to a function call (does not escape)
//     * The source Permission outlives the view (ALWAYS — by lifetime)
//     * Refcounting would be wasteful (no need to track lifetime at runtime)
//     * Multiple in-flight readers all reference the SAME source
//
//   Use SharedPermission via SharedPermissionPool when:
//     * The borrow ESCAPES to a long-lived consumer thread
//     * Lifetime must be tracked at runtime (the consumer may outlive
//       the producer of the original Permission)
//     * Mode transitions (shared-read → exclusive-write) are needed
//     * The Pool's atomic refcount IS the lifetime-tracking machinery
//
// ─── Why "lifetime-bound" — and the C++26 enforcement story ─────────
//
// `mint_read_view(Permission<Tag> const& p [[gnu::lifetimebound]]) → ReadView<Tag>`
//
// The CRUCIBLE_LIFETIMEBOUND attribute on the parameter tells the
// compiler "the returned object's lifetime is bound to p's lifetime."
// When user writes:
//
//   auto v = mint_read_view(make_permission());  // BUG: temporary
//
// the temporary `make_permission()` is destroyed at the end of the
// statement, leaving `v` referencing a dead Permission.  Clang
// (with `-Wdangling-reference`) catches this.  GCC 16 currently does
// not implement the lifetimebound attribute (the macro expands to
// nothing on GCC); when Clang ships parity for this codebase, the
// check goes from "documented discipline" to "compile error."
//
// In the meantime, ReadView's value is:
//   * Type-system documentation of the borrow pattern
//   * Compile-time API discrimination (read vs write at the type level)
//   * Concept compliance for generic algorithms (CSL terminology in C++)
//
// ─── Ergonomics: ReadView is COPYABLE but NOT REASSIGNABLE ──────────
//
// Multiple borrowers may coexist freely — that's the point of a read
// view.  But assignment is deleted with a reason: reassigning a
// ReadView would silently swap the underlying source identity, hiding
// the lifetime relationship from review.  If you need to "rebind,"
// construct a fresh ReadView via mint_read_view.
//
// `operator new` deleted — heap allocation defeats the lifetime
// contract.  ReadView must live on the stack or as a structured-
// binding member; never `new ReadView(...)` and never store in a
// long-lived collection.
//
// ─── Composition with Tier 2 Workload primitives ────────────────────
//
// A common pattern: a parallel_for_views worker receives an
// OwnedRegion<T, Slice<Whole, I>> for write access to its slice, but
// also wants to ATOMICALLY observe a separate piece of immutable
// configuration (e.g. a tuning constant).  Pass the tuning constant
// as a `ReadView<TuningCfg>` — the type system proves the worker
// only reads it, and lifetime is structurally bounded by the parent
// scope's Permission<TuningCfg>.

#include <crucible/Platform.h>
#include <crucible/permissions/Permission.h>

#include <concepts>
#include <cstddef>
#include <type_traits>

namespace crucible::safety {

// Forward declaration of mint_read_view so the class can friend it
// before its definition.
template <typename Tag>
class ReadView;
template <typename Tag>
[[nodiscard]] constexpr ReadView<Tag>
mint_read_view(Permission<Tag> const& p CRUCIBLE_LIFETIMEBOUND) noexcept;

}  // namespace crucible::safety

// Forward declaration of the session-layer Borrowed payload carrier.
// `proto::Borrowed<T, Tag>` embeds a `ReadView<Tag>` as the type-level
// proof that the recipient may READ the borrowed resource for the
// duration of one protocol step; it is a legitimate issuer of a
// default-constructed view (see sessions/SessionPermPayloads.h).
namespace crucible::safety::proto {
template <typename T, typename Tag>
struct Borrowed;
}  // namespace crucible::safety::proto

namespace crucible::safety {

// ── ReadView<Tag> ────────────────────────────────────────────────────

template <typename Tag>
class [[nodiscard]] ReadView {
public:
    using tag_type = Tag;

    // Copyable: multiple borrowers OK, all referencing the same source.
    constexpr ReadView(const ReadView&) noexcept            = default;
    constexpr ReadView(ReadView&&) noexcept                 = default;

    // Assignment deleted with reason: reassigning would silently swap
    // the underlying source identity, hiding the lifetime relationship
    // from review.  Construct a fresh ReadView via mint_read_view instead.
    ReadView& operator=(const ReadView&)
        = delete("ReadView is single-binding; rebinding hides lifetime relationships — construct a fresh view via mint_read_view");
    ReadView& operator=(ReadView&&)
        = delete("ReadView is single-binding; rebinding hides lifetime relationships");

    ~ReadView() = default;

    // Heap allocation deleted — defeats the lifetime contract.  The
    // ReadView is meant to live on the stack or as a function/lambda
    // parameter; storing it in a long-lived collection would let it
    // outlive the source Permission.
    static void* operator new(std::size_t)
        = delete("ReadView must live on the stack; heap allocation defeats the lifetime contract");
    static void* operator new[](std::size_t)
        = delete("ReadView arrays on the heap defeat the lifetime contract");
    static void* operator new(std::size_t, std::align_val_t)
        = delete("ReadView must live on the stack");
    static void* operator new[](std::size_t, std::align_val_t)
        = delete("ReadView arrays on the heap defeat the lifetime contract");
    static void operator delete(void*)                     = delete;
    static void operator delete[](void*)                   = delete;
    static void operator delete(void*, std::align_val_t)   = delete;
    static void operator delete[](void*, std::align_val_t) = delete;

private:
    // Private default constructor.  Mirrors Permission<Tag> /
    // SharedPermission<Tag>: a read-borrow PROOF you can fabricate from
    // nothing is not a proof.  Only the friended issuers below construct
    // a fresh ReadView, and every authorization site is grep-discoverable
    // via the `mint_read_view` name (or the narrow session-layer carrier).
    //
    // ReadView remains an empty class — sizeof == 1, EBO-collapses to 0
    // as a [[no_unique_address]] member.  Copy/move ctors stay public, so
    // embedding a *minted* view (e.g. WorkerHandle copying `cv`) is
    // unaffected; only construction-from-nothing is closed.
    constexpr ReadView() noexcept = default;

    // Friend access list — kept short on purpose.  Each addition is a new
    // way to mint a ReadView and demands review.

    // The single chokepoint factory.  Derives a fresh view from a held
    // parent Permission<Tag> whose lifetime bounds the view.
    friend constexpr ReadView<Tag>
    mint_read_view<Tag>(Permission<Tag> const& p CRUCIBLE_LIFETIMEBOUND) noexcept;

    // Session-layer borrow carrier.  `proto::Borrowed<T, Tag>` embeds a
    // ReadView<Tag> as the recipient's read proof for one protocol step;
    // it default-constructs the embedded view in its aggregate ctor.  The
    // friend is narrow (one class template) and the soundness story is the
    // session protocol's PermSet evolution, not ad-hoc construction.
    template <typename T, typename FriendTag>
    friend struct ::crucible::safety::proto::Borrowed;
};

// ── mint_read_view — the single chokepoint factory ──────────────────
//
// §XXI Universal Mint Pattern: a cross-tier composition factory that
// derives a fresh `ReadView<Tag>` token from a parent `Permission<Tag>`.
// Named `mint_*` so `grep "mint_"` finds every authorization point;
// the parent Permission is the load-bearing soundness gate (its
// lifetime bounds the returned view's lifetime via the
// CRUCIBLE_LIFETIMEBOUND attribute, and its Tag identity is checked
// at the type-system level).
//
// CRUCIBLE_LIFETIMEBOUND on `p` tells the compiler "the returned
// ReadView's lifetime is bounded by p's."  Under Clang (when added),
// dangling-reference cases are rejected at compile time:
//
//   auto v = mint_read_view(make_permission());   // -Wdangling-reference
//
// Under GCC 16 the attribute is silently ignored (the macro expands
// to nothing).  Discipline + review fill the gap until parity lands.

template <typename Tag>
[[nodiscard]] constexpr ReadView<Tag>
mint_read_view(Permission<Tag> const& p CRUCIBLE_LIFETIMEBOUND) noexcept {
    (void)p;  // observed; ReadView is proof-only
    return ReadView<Tag>{};
}

// ── with_read_view — RAII scoped-borrow helper ──────────────────────
//
// Mints a ReadView, invokes body with the view, releases on return.
// Body's signature: R(ReadView<Tag>) where R is anything (incl void).
// Return value (if any) is forwarded to the caller.
//
// Useful for the canonical scoped-read pattern:
//
//   auto value = with_read_view(my_perm,
//       [&](ReadView<Tag>) noexcept { return compute_from(data); });
//
// The lambda receives a copyable ReadView; multiple captures of the
// view inside the lambda are fine (all reference the same source).

template <typename Tag, typename Body>
    requires std::is_invocable_v<Body, ReadView<Tag>>
[[nodiscard]] constexpr auto
with_read_view(Permission<Tag> const& p CRUCIBLE_LIFETIMEBOUND, Body&& body)
    noexcept(std::is_nothrow_invocable_v<Body, ReadView<Tag>>)
    -> std::invoke_result_t<Body, ReadView<Tag>>
{
    return body(mint_read_view(p));
}

// ── Zero-cost guarantees ────────────────────────────────────────────

namespace detail {
    struct read_view_test_tag {};
}

// Empty class: 1 byte minimum.  EBO-collapses to 0 in containing types.
static_assert(sizeof(ReadView<detail::read_view_test_tag>) == 1,
              "ReadView<Tag> must be a 1-byte empty class");

// Trivially copyable / destructible.
static_assert(std::is_trivially_copyable_v<ReadView<detail::read_view_test_tag>>,
              "ReadView<Tag> must be trivially copyable (zero-cost copy)");
static_assert(std::is_trivially_destructible_v<ReadView<detail::read_view_test_tag>>,
              "ReadView<Tag> destructor must be trivial");

// Copyable but not assignable.
static_assert(std::is_copy_constructible_v<ReadView<detail::read_view_test_tag>>,
              "ReadView<Tag> MUST be copy-constructible (multi-reader semantics)");
static_assert(!std::is_copy_assignable_v<ReadView<detail::read_view_test_tag>>,
              "ReadView<Tag> must NOT be copy-assignable (single-binding)");
static_assert(std::is_move_constructible_v<ReadView<detail::read_view_test_tag>>,
              "ReadView<Tag> MUST be move-constructible");
static_assert(!std::is_move_assignable_v<ReadView<detail::read_view_test_tag>>,
              "ReadView<Tag> must NOT be move-assignable (single-binding)");

}  // namespace crucible::safety
