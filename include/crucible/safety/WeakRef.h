#pragma once

// ── crucible::safety::WeakRef<T> ────────────────────────────────────
//
// Non-owning, NULLABLE, may-be-evicted reference to an object owned
// elsewhere.  The "cached back-pointer" primitive: a slot that starts
// empty, is populated with a borrowed pointer the holder does NOT own,
// and may be reset (evicted) — forcing every access through a checked
// path.
//
// ── Why WeakRef is NOT BorrowedRef ──────────────────────────────────
//
// BorrowedRef<T> (safety/Borrowed.h) is the must-be-present borrow: it
// has no default ctor and derefs unconditionally — its own doc says "if
// a caller wants may-be-null borrow semantics, they should use a
// separate nullable type."  WeakRef IS that type.  The two are
// complementary, not redundant:
//
//   BorrowedRef<T>   — non-owning, ALWAYS valid, unconditional deref.
//   Borrowed<T, Src> — non-owning span, owner-tagged.
//   ScopedView<C,Tag>— scope-bounded inspect borrow.
//   WeakRef<T>       — non-owning, MAY be null/evicted, checked deref.
//
// ── "Lifetime-checked" means null-discipline, NOT a control block ────
//
// Crucible bans std::weak_ptr (atomic refcount + heap, §IV opt-out), so
// WeakRef does NOT detect referent expiry at runtime — there is no
// control block.  The "checked" is structural: try_get() hands back a
// nullable T* the caller MUST inspect, and get()/operator*/operator->
// are CRUCIBLE_PRE(ptr_ != nullptr)-guarded so a forgotten null-check
// aborts loudly (enforce) or is an [[assume]] the caller promised
// (ignore) — never silent UB.  Keeping the referent alive while a
// WeakRef points at it is the OWNER's contract (e.g., RegionCache
// invalidates a slot before the DAG evicts the RegionNode it borrowed).
//
// ── Production call sites (per WRAP-* tasks) ────────────────────────
//
//   #986 WRAP-RegionCache-1: regions_[8] raw RegionNode* cache slots →
//                            WeakRef<RegionNode> (slots start empty,
//                            hold DAG-owned nodes, may be overwritten).
//   #993 WRAP-RE-1:          ReplayEngine cursors / parent back-pointer
//                            → WeakRef on the parent RegionNode.
//
// ── Public API ──────────────────────────────────────────────────────
//
//   Construction:
//     WeakRef()                  — NSDMI: empty (null) slot.
//     explicit WeakRef(T& ref)   — bind to a live object.
//     from_raw(T* p)             — bind from a raw nullable pointer
//                                  (the populate-from-a-field path);
//                                  null is a valid, expected input.
//
//   Queries:
//     has_value()                — is the slot occupied (non-null)?
//     explicit operator bool()   — same, for `if (wr)`.
//
//   Access:
//     try_get()                  — T* (may be null).  The SAFE primary
//                                  accessor AND the FFI/escape path; no
//                                  precondition, caller inspects.
//     get()                      — T& ; CRUCIBLE_PRE(non-null).
//     operator*() / operator->() — T& / T* ; CRUCIBLE_PRE(non-null).
//
//   Mutation:
//     reset()                    — evict: set to null.
//     (rebind via copy-assign: `wr = WeakRef::from_raw(p)`.)
//
//   Equality:
//     operator==                 — pointer IDENTITY (two WeakRefs are
//                                  equal iff they point at the same
//                                  object; two empties are equal).
//
// ── Eight-axiom audit ───────────────────────────────────────────────
//
//   InitSafe — ptr_ has an NSDMI (nullptr); the default ctor yields a
//              deterministic empty slot, not an indeterminate pointer.
//   TypeSafe — distinct newtype: no implicit conversion to/from T*
//              (try_get()/from_raw are the named, explicit escapes), so
//              a WeakRef cannot silently decay to a raw owning-looking
//              pointer.  Constrained to object T (rejects T&/void/fn).
//   NullSafe — try_get() is the no-deref nullable accessor;
//              get()/operator*/operator-> are CRUCIBLE_PRE-guarded so a
//              null deref aborts under enforce rather than UB.
//   MemSafe  — non-owning: no alloc, no free, no double-free of the
//              wrapper.  (Referent dangling is the owner's contract;
//              see the control-block note above — NOT detected here.)
//   BorrowSafe — copy = two independent readers of the same referent
//              (no ownership conflict); identical to BorrowedRef.
//   ThreadSafe — value type; cross-thread publication uses atomics, not
//              this.  A WeakRef cache (RegionCache) is foreground-only.
//   LeakSafe — owns no resource.
//   DetSafe  — pure pointer identity; no hidden state, no ordering.
//
// ── Runtime cost ────────────────────────────────────────────────────
//
//   sizeof(WeakRef<T>) == sizeof(T*).  Every accessor is a load + (for
//   the guarded derefs) a branch that vanishes under
//   contract-semantic=ignore.  Same machine code as a bare nullable
//   T* with a hand-written null-check.
//
// ── Why structural (not Graded) ─────────────────────────────────────
//
// A nullable non-owning pointer has no lattice — it is null or it points
// somewhere; there is no useful ordering or modality.  Deliberately-not-
// graded structural wrapper per CLAUDE.md §XVI, peer to BorrowedRef.
//
// ── References ──────────────────────────────────────────────────────
//
//   CLAUDE.md §II        — 8 axioms
//   CLAUDE.md §XVI       — safety wrapper catalog (structural family)
//   CLAUDE.md §XVIII HS14 — neg-compile fixture requirement (≥2)
//   safety/Borrowed.h    — BorrowedRef (the must-be-present sibling)
//   safety/Mutation.h    — WriteOnceNonNull (guarded-deref analog)

#include <crucible/Platform.h>      // CRUCIBLE_LIFETIMEBOUND
#include <crucible/safety/Pre.h>    // CRUCIBLE_PRE

#include <cstdlib>                  // std::abort (runtime_smoke_test)
#include <string_view>
#include <type_traits>

namespace crucible::safety {

// ═════════════════════════════════════════════════════════════════════
// ── WeakRef<T> ─────────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <class T>
    requires (std::is_object_v<T>)   // rejects T&, void, function types
class [[nodiscard]] WeakRef {
public:
    using element_type = T;

    static constexpr std::string_view wrapper_kind() noexcept {
        return "structural::WeakRef";
    }

private:
    // NSDMI null = "empty / evicted slot" (InitSafe).  Every reachable
    // ctor sets ptr_ explicitly; this guarantees a deterministic null
    // for the default ctor and any future aggregate path.
    T* ptr_ = nullptr;

    struct from_raw_tag_t {};
    constexpr WeakRef(from_raw_tag_t, T* p) noexcept : ptr_{p} {}

public:
    // ── Construction ────────────────────────────────────────────────

    // Empty slot (null).  This is the deliberate difference from
    // BorrowedRef, which deletes its default ctor — a WeakRef models a
    // cache slot that legitimately starts (and may return to) empty.
    constexpr WeakRef() noexcept = default;

    // Bind to a live object.  Lifetime-bound: a temporary triggers the
    // dangling diagnostic where the compiler supports it.
    constexpr explicit WeakRef(T& ref CRUCIBLE_LIFETIMEBOUND) noexcept
        : ptr_{&ref} {}

    // Populate from a raw nullable pointer (the cache-slot-from-a-field
    // path).  Unlike BorrowedRef::from_raw_nonnull, null is a VALID,
    // expected input — no precondition.  Grep-discoverable ("from_raw").
    [[nodiscard]] static constexpr WeakRef from_raw(
        T* p CRUCIBLE_LIFETIMEBOUND) noexcept {
        return WeakRef{from_raw_tag_t{}, p};
    }

    // Defaulted copy/move/dtor — non-owning, so a copy is two readers
    // of the same referent (no ownership conflict).
    constexpr WeakRef(WeakRef const&)            = default;
    constexpr WeakRef(WeakRef&&)                 = default;
    constexpr WeakRef& operator=(WeakRef const&) = default;
    constexpr WeakRef& operator=(WeakRef&&)      = default;
    ~WeakRef()                                   = default;

    // ── Queries ─────────────────────────────────────────────────────

    [[nodiscard]] constexpr bool has_value() const noexcept {
        return ptr_ != nullptr;
    }
    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return ptr_ != nullptr;
    }

    // ── Access ──────────────────────────────────────────────────────

    // Safe nullable accessor — returns the raw pointer, which may be
    // null.  The PRIMARY access path: the caller inspects the result.
    // Also the explicit FFI/interop escape (grep-discoverable).
    [[nodiscard]] constexpr T* try_get() const noexcept { return ptr_; }

    // Guarded derefs — CRUCIBLE_PRE fires on a null slot.  Use after a
    // has_value()/try_get() check, or when the slot is known occupied.
    [[nodiscard]] constexpr T& get() const noexcept {
        CRUCIBLE_PRE(ptr_ != nullptr);
        return *ptr_;
    }
    [[nodiscard]] constexpr T& operator*() const noexcept {
        CRUCIBLE_PRE(ptr_ != nullptr);
        return *ptr_;
    }
    [[nodiscard]] constexpr T* operator->() const noexcept {
        CRUCIBLE_PRE(ptr_ != nullptr);
        return ptr_;
    }

    // ── Mutation ────────────────────────────────────────────────────

    // Evict: return the slot to the empty state.  (To rebind to a new
    // referent, copy-assign a fresh WeakRef: `wr = WeakRef::from_raw(p)`
    // or `wr = WeakRef{ref}`.)
    constexpr void reset() noexcept { ptr_ = nullptr; }

    // Equality is pointer IDENTITY, not value: two WeakRefs are equal
    // iff they refer to the same object (or are both empty).
    [[nodiscard]] friend constexpr bool operator==(
        WeakRef a, WeakRef b) noexcept {
        return a.ptr_ == b.ptr_;
    }
};

// ═════════════════════════════════════════════════════════════════════
// ── Layout + type-identity invariants ──────────────────────────────
// ═════════════════════════════════════════════════════════════════════

// Zero-cost: collapses to exactly one pointer.
static_assert(sizeof(WeakRef<int>)  == sizeof(int*));
static_assert(alignof(WeakRef<int>) == alignof(int*));
static_assert(std::is_trivially_copyable_v<WeakRef<int>>);
static_assert(std::is_trivially_destructible_v<WeakRef<int>>);

// Nullable-by-default — the intentional difference from BorrowedRef
// (which deletes its default ctor).
static_assert(std::is_default_constructible_v<WeakRef<int>>);

// TypeSafe: no implicit decay to a raw pointer, no implicit construction
// from a reference.  try_get()/from_raw are the named explicit escapes.
static_assert(!std::is_convertible_v<WeakRef<int>, int*>);
static_assert(!std::is_convertible_v<int&, WeakRef<int>>);

// ═════════════════════════════════════════════════════════════════════
// ── Self-test ─────────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail::weak_ref_self_test {

// Default-constructed WeakRef is empty.
[[nodiscard]] consteval bool default_is_empty() noexcept {
    WeakRef<int> w{};
    return !w.has_value() && !static_cast<bool>(w) && w.try_get() == nullptr;
}
static_assert(default_is_empty());

// Bind to a live object; queries + derefs reflect it.
[[nodiscard]] consteval bool binds_and_derefs() noexcept {
    int x = 42;
    WeakRef<int> w{x};
    return w.has_value()
        && static_cast<bool>(w)
        && w.try_get() == &x
        && w.get() == 42
        && *w == 42;
}
static_assert(binds_and_derefs());

// operator-> reaches a struct member.
[[nodiscard]] consteval bool arrow_reaches_member() noexcept {
    struct Pair { int a; int b; };
    Pair p{7, 9};
    WeakRef<Pair> w{p};
    return w->a == 7 && w->b == 9;
}
static_assert(arrow_reaches_member());

// from_raw accepts null (empty) AND non-null (occupied).
[[nodiscard]] consteval bool from_raw_is_nullable() noexcept {
    WeakRef<int> empty = WeakRef<int>::from_raw(nullptr);
    int x = 5;
    WeakRef<int> full = WeakRef<int>::from_raw(&x);
    return !empty.has_value()
        && full.has_value()
        && full.try_get() == &x;
}
static_assert(from_raw_is_nullable());

// reset() evicts back to empty.
[[nodiscard]] consteval bool reset_evicts() noexcept {
    int x = 1;
    WeakRef<int> w{x};
    if (!w.has_value()) return false;
    w.reset();
    return !w.has_value() && w.try_get() == nullptr;
}
static_assert(reset_evicts());

// Equality is identity; copy is two readers of the same referent.
[[nodiscard]] consteval bool identity_equality_and_copy() noexcept {
    int x = 3;
    int y = 3;                       // same value, different object
    WeakRef<int> a{x};
    WeakRef<int> b = a;              // copy → two readers
    WeakRef<int> c{y};
    return a == b                    // same object
        && !(a == c)                 // different object, equal value
        && a.try_get() == b.try_get()
        && WeakRef<int>{} == WeakRef<int>{};  // two empties equal
}
static_assert(identity_equality_and_copy());

// Wrapper-kind diagnostic.
static_assert(WeakRef<int>::wrapper_kind() == "structural::WeakRef");

// ── Runtime smoke test ──────────────────────────────────────────────
//
// Per feedback_algebra_runtime_smoke_test_discipline: exercise every
// named op with NON-constant input so a consteval-vs-runtime divergence
// in the guarded-deref / null arithmetic would surface here.
inline void runtime_smoke_test() {
    WeakRef<int> empty{};
    if (empty.has_value() || static_cast<bool>(empty)) std::abort();
    if (empty.try_get() != nullptr) std::abort();

    volatile int seed = 77;
    int box = static_cast<int>(seed);          // non-constant
    WeakRef<int> w{box};
    if (!w.has_value() || !static_cast<bool>(w)) std::abort();
    if (w.try_get() != &box) std::abort();
    if (w.get() != box) std::abort();
    if (*w != box) std::abort();

    // operator-> on a struct referent.
    struct Pair { int a; int b; };
    Pair p{static_cast<int>(seed), static_cast<int>(seed) + 1};
    WeakRef<Pair> wp{p};
    if (wp->a != box || wp->b != box + 1) std::abort();

    // reset evicts.
    w.reset();
    if (w.has_value() || w.try_get() != nullptr) std::abort();

    // from_raw nullable both ways.
    WeakRef<int> fr_null = WeakRef<int>::from_raw(nullptr);
    if (fr_null.has_value()) std::abort();
    WeakRef<int> fr_full = WeakRef<int>::from_raw(&box);
    if (!fr_full.has_value() || fr_full.try_get() != &box) std::abort();

    // identity equality + copy = two readers.
    WeakRef<int> a{box};
    WeakRef<int> b = a;
    if (!(a == b) || a.try_get() != b.try_get()) std::abort();
}

}  // namespace detail::weak_ref_self_test

}  // namespace crucible::safety
