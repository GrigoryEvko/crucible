#pragma once

// ── crucible::safety::{BorrowedRef, Borrowed} ───────────────────────
//
// Non-owning reference wrappers with phantom source-tagging.  Replace
// raw `T*` / `const char*` / `std::span<T>` fields where the wrapper
// semantically BORROWS from a longer-lived owner without participating
// in ownership.
//
// ── Two distinct shapes ─────────────────────────────────────────────
//
//   BorrowedRef<T>           — borrowed reference to a SINGLE object.
//                              Semantically `T&` but storable.  Never
//                              null after construction (enforced by
//                              the API: no default ctor, both
//                              construction paths require a real
//                              binding).  Source is implicit in T
//                              (BorrowedRef<Arena> means "an Arena
//                              borrowed from somewhere").
//
//   Borrowed<T, Source>      — borrowed SPAN over T memory, tagged with
//                              the owner type Source.  Distinguishes
//                              "char span borrowed from ExprPool" from
//                              "char span borrowed from SchemaTable" at
//                              the type level.  Source is REQUIRED —
//                              the whole point is forcing reviewers to
//                              think about who owns the memory.
//
// ── Production call sites (per WRAP-* tasks) ────────────────────────
//
//   #913  Expr::as_symbol_name() raw const char*   →
//             Borrowed<const char, ExprPool>
//   #972  RecipePool arena_ raw borrow              →
//             BorrowedRef<Arena>
//   #980  RecipeReg RecipePool* dependency          →
//             BorrowedRef<RecipePool>
//   #995  ReplayEngine PoolAllocator*               →
//             BorrowedRef<PoolAllocator>
//   #1027 SwissTable load(int8_t*) raw pointer      →
//             Borrowed<ControlByte, SwissCtrl>
//
// All five sites today use raw pointer / span fields with NO compile-
// time provenance.  A refactor that swaps two unrelated borrows
// (e.g., handing a SchemaTable's char* into an ExprPool-expecting
// caller) compiles silently — the wrapper rejects the swap because
// the Source type differs.
//
// ── Eight-axiom audit ───────────────────────────────────────────────
//
//   InitSafe — BorrowedRef has no default ctor (UINT-style sentinels
//              forbidden); Borrowed has NSDMI = empty span (which IS
//              a valid empty borrowed range, mirroring std::span{}).
//   TypeSafe — Source phantom on Borrowed prevents cross-source mixing
//              at the type level.  BorrowedRef<T1> and BorrowedRef<T2>
//              are different instantiations, no conversion possible.
//   NullSafe — BorrowedRef enforces non-null via its construction
//              paths (T& binding cannot be null; from_raw_nonnull
//              has a contract pre).  Borrowed accepts null+0 (an
//              empty span — the `nullptr, 0` valid case).
//   MemSafe  — non-owning; defaulted copy/move/destroy (RAII
//              responsibility lies with the owner type, not the
//              wrapper).
//   BorrowSafe — wrapper IS the borrow; non-owning by design;
//              Source phantom flags every site as "I'm a borrow".
//   ThreadSafe — pure value type (pointer + maybe size).  Per-thread
//              copies are cheap; cross-thread requires the underlying
//              T's own thread-safety story.
//   LeakSafe — non-owning; nothing to leak.
//   DetSafe  — pure structural; no FP, no allocations, no kernel.
//
// ── Runtime cost ────────────────────────────────────────────────────
//
//   sizeof(BorrowedRef<T>)         == sizeof(T*)
//   sizeof(Borrowed<T, Source>)    == sizeof(std::span<T>)
//                                  == sizeof(T*) + sizeof(std::size_t)
//
//   Both are trivially_copyable.  Every accessor compiles to a single
//   load (no indirection through a wrapper).  Verified by static_asserts
//   at the end of the header.
//
// ── Why structural (not Graded) ─────────────────────────────────────
//
// Borrowing semantics are STRUCTURAL: the wrapper records "I borrow
// from a longer-lived owner."  No lattice operation applies — the
// Source phantom isn't a comparable axis (BorrowedRef<Arena> doesn't
// "subsume" or "weaken-to" BorrowedRef<RecipePool>).  Joins ScopedView,
// Pinned, Machine as a deliberately-not-graded structural wrapper.
//
// ── Lifetime annotations ────────────────────────────────────────────
//
// Both wrappers' constructors carry CRUCIBLE_LIFETIMEBOUND on their
// reference / pointer parameters.  On Clang the annotation triggers
// -Wdangling diagnostics when a temporary's lifetime would end before
// the wrapper's; on GCC the macro is a no-op (the annotation isn't
// supported there yet) but the doc-level intent is preserved and the
// Clang CI would catch dangling cases.
//
// ── References ──────────────────────────────────────────────────────
//
//   CLAUDE.md §II        — 8 axioms
//   CLAUDE.md §XVI       — safety wrapper catalog (structural family)
//   CLAUDE.md §XVIII HS14 — neg-compile fixture requirement (≥2)
//   include/crucible/Platform.h — CRUCIBLE_LIFETIMEBOUND macro

#include <crucible/Platform.h>

#include <cstddef>
#include <cstdlib>
#include <span>
#include <string_view>
#include <type_traits>

namespace crucible::safety {

// ═════════════════════════════════════════════════════════════════════
// ── BorrowedRef<T> — non-owning single-object reference ──────────
// ═════════════════════════════════════════════════════════════════════

template <class T>
class [[nodiscard]] BorrowedRef {
public:
    using element_type = T;

    static constexpr std::string_view wrapper_kind() noexcept {
        return "structural::BorrowedRef";
    }

private:
    T* ptr_;

    struct from_raw_tag_t {};
    constexpr BorrowedRef(from_raw_tag_t, T* p) noexcept : ptr_{p} {}

public:
    // ── Construction ────────────────────────────────────────────────
    //
    // No default ctor — a BorrowedRef MUST refer to a real object.  A
    // null BorrowedRef would defeat the whole purpose (callers would
    // have to null-check at every dereference).  If a caller wants
    // "may-be-null borrow" semantics, they should use Optional<
    // BorrowedRef<T>> explicitly.

    BorrowedRef() = delete;

    // Bind from a real reference.  Lifetime-bound: temporaries cannot
    // be passed in without compiler diagnostic (Clang: -Wdangling;
    // GCC: doc-only intent).
    constexpr explicit BorrowedRef(T& ref CRUCIBLE_LIFETIMEBOUND) noexcept
        : ptr_{&ref} {}

    // Explicit raw-pointer escape — for paths that hold a raw
    // pointer for whatever reason (e.g., reading from a struct field
    // populated by a C API).  Caller must witness non-null via
    // contract pre.
    [[nodiscard]] static constexpr BorrowedRef from_raw_nonnull(
        T* p CRUCIBLE_LIFETIMEBOUND) noexcept
        pre (p != nullptr)
    {
        return BorrowedRef{from_raw_tag_t{}, p};
    }

    // Defaulted copy/move (non-owning — copy is two readers, no
    // ownership conflict).
    constexpr BorrowedRef(BorrowedRef const&)            = default;
    constexpr BorrowedRef(BorrowedRef&&)                 = default;
    constexpr BorrowedRef& operator=(BorrowedRef const&) = default;
    constexpr BorrowedRef& operator=(BorrowedRef&&)      = default;
    ~BorrowedRef()                                       = default;

    // ── Access ──────────────────────────────────────────────────────
    [[nodiscard]] constexpr T& get() const noexcept { return *ptr_; }
    [[nodiscard]] constexpr T& operator*() const noexcept { return *ptr_; }
    [[nodiscard]] constexpr T* operator->() const noexcept { return ptr_; }

    // Explicit raw-pointer escape for FFI / interop.  Grep-discoverable
    // ("raw_ptr") for review.
    [[nodiscard]] constexpr T* raw_ptr() const noexcept { return ptr_; }

    // Equality compares the pointed-to identity (NOT the value).  Two
    // BorrowedRef instances are equal iff they refer to the same
    // object.
    [[nodiscard]] friend constexpr bool operator==(
        BorrowedRef a, BorrowedRef b) noexcept {
        return a.ptr_ == b.ptr_;
    }
};

// ═════════════════════════════════════════════════════════════════════
// ── Borrowed<T, Source> — non-owning span tagged with owner ──────
// ═════════════════════════════════════════════════════════════════════

template <class T, class Source>
class [[nodiscard]] Borrowed {
public:
    using element_type = T;
    using source_type  = Source;
    using span_type    = std::span<T>;

    static constexpr std::string_view wrapper_kind() noexcept {
        return "structural::Borrowed";
    }

private:
    span_type span_{};

public:
    // ── Construction ────────────────────────────────────────────────
    //
    // Default ctor: empty span (mirrors std::span{}).  Useful as
    // sentinel for "no borrow yet" without resorting to nullptr +
    // separate flag.

    constexpr Borrowed() noexcept = default;

    // Bind from an existing span.
    constexpr explicit Borrowed(span_type span CRUCIBLE_LIFETIMEBOUND) noexcept
        : span_{span} {}

    // Bind from pointer + count.  An empty borrow is `(nullptr, 0)`
    // — a valid std::span construction.
    constexpr Borrowed(T* data CRUCIBLE_LIFETIMEBOUND, std::size_t count) noexcept
        : span_{data, count} {}

    // Bind from a contiguous range expressing the borrow site
    // explicitly (typical: a member array).
    template <std::size_t N>
    constexpr explicit Borrowed(T (&array CRUCIBLE_LIFETIMEBOUND)[N]) noexcept
        : span_{array, N} {}

    // Defaulted copy/move (non-owning).
    constexpr Borrowed(Borrowed const&)            = default;
    constexpr Borrowed(Borrowed&&)                 = default;
    constexpr Borrowed& operator=(Borrowed const&) = default;
    constexpr Borrowed& operator=(Borrowed&&)      = default;
    ~Borrowed()                                    = default;

    // ── Access ──────────────────────────────────────────────────────
    [[nodiscard]] constexpr T*           data()    const noexcept { return span_.data(); }
    [[nodiscard]] constexpr std::size_t  size()    const noexcept { return span_.size(); }
    [[nodiscard]] constexpr bool         empty()   const noexcept { return span_.empty(); }
    [[nodiscard]] constexpr T*           begin()   const noexcept { return span_.data(); }
    [[nodiscard]] constexpr T*           end()     const noexcept { return span_.data() + span_.size(); }
    // Bounds preconditions are documented but not contract-checked
    // here — std::span's operator[] / front() / back() are UB on
    // out-of-range, and -D_GLIBCXX_DEBUG enables the libstdc++
    // bounds-check overlay.  Contract `pre` clauses on these accessors
    // would force the predicate to be a constant expression in
    // consteval contexts (P2900 + GCC 16 currently rejects), and the
    // self-test exercises them at consteval — so the pre is left as
    // a documented invariant.
    [[nodiscard]] constexpr T&           front()   const noexcept { return span_.front(); }
    [[nodiscard]] constexpr T&           back()    const noexcept { return span_.back(); }
    [[nodiscard]] constexpr T& operator[](std::size_t i) const noexcept { return span_[i]; }

    // Explicit span escape for paths that need to interop with span-
    // accepting APIs.
    [[nodiscard]] constexpr span_type as_span() const noexcept { return span_; }

    // ── subview — source-preserving sub-extent extraction ─────────
    //
    // Returns a Borrowed<T, Source> covering [offset, offset + count)
    // of this Borrowed's range.  PRESERVES the Source phantom — a
    // sub-window of a RegionNode borrow is still a RegionNode borrow.
    //
    // Without this, callers slicing a Borrowed had to escape via
    // .as_span().subspan(...) which produces a bare std::span<T> and
    // loses the Source tag.  With subview() the Source propagates
    // automatically through the slice operation.
    //
    // Bounds invariant: offset + count ≤ size().  Not contract-
    // checked here (gotcha-6b: pre clauses on constexpr members
    // called from consteval contexts blow up).  std::span::subspan's
    // UB-on-out-of-range + libstdc++ -D_GLIBCXX_DEBUG bounds-overlay
    // catch at runtime.
    [[nodiscard]] constexpr Borrowed
    subview(std::size_t offset, std::size_t count) const noexcept
    {
        return Borrowed{span_.subspan(offset, count)};
    }

    // Equality compares (data, size) pair — two Borrowed instances
    // are equal iff they cover the same memory range.  Does NOT
    // compare element values.
    [[nodiscard]] friend constexpr bool operator==(
        Borrowed a, Borrowed b) noexcept {
        return a.span_.data() == b.span_.data()
            && a.span_.size() == b.span_.size();
    }
};

// ═════════════════════════════════════════════════════════════════════
// ── Layout invariants ─────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail::borrowed_layout {

struct OwnerA { int dummy = 0; };
struct OwnerB { int dummy = 0; };

}  // namespace detail::borrowed_layout

static_assert(sizeof(BorrowedRef<int>)            == sizeof(int*));
static_assert(sizeof(BorrowedRef<double>)         == sizeof(double*));
static_assert(sizeof(Borrowed<int, detail::borrowed_layout::OwnerA>)
              == sizeof(std::span<int>));
static_assert(sizeof(Borrowed<const char, detail::borrowed_layout::OwnerA>)
              == sizeof(std::span<const char>));

static_assert(std::is_trivially_copyable_v<BorrowedRef<int>>);
static_assert(std::is_trivially_copyable_v<Borrowed<int, detail::borrowed_layout::OwnerA>>);
static_assert(std::is_trivially_destructible_v<BorrowedRef<int>>);
static_assert(std::is_trivially_destructible_v<Borrowed<int, detail::borrowed_layout::OwnerA>>);

// BorrowedRef<T> has NO default ctor (load-bearing — prevents null
// sentinel state).  This static_assert catches a regression that
// would re-add the default ctor without thinking through the null
// state semantics.
static_assert(!std::is_default_constructible_v<BorrowedRef<int>>,
    "BorrowedRef<T> MUST NOT be default-constructible — no null state. "
    "If this fires, a refactor re-introduced the default ctor and the "
    "wrapper now admits a null sentinel that the API doesn't handle.");

// Borrowed<T, S> IS default-constructible (empty span is a valid borrow).
static_assert(std::is_default_constructible_v<
    Borrowed<int, detail::borrowed_layout::OwnerA>>);

// ═════════════════════════════════════════════════════════════════════
// ── Self-test ─────────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail::borrowed_self_test {

using OwnerA = ::crucible::safety::detail::borrowed_layout::OwnerA;
using OwnerB = ::crucible::safety::detail::borrowed_layout::OwnerB;

// ── BorrowedRef<T> ──────────────────────────────────────────────────

[[nodiscard]] consteval bool ref_binds_and_dereferences() noexcept {
    int x = 42;
    BorrowedRef<int> r{x};
    return r.get() == 42 && *r == 42 && r.raw_ptr() == &x;
}
static_assert(ref_binds_and_dereferences());

[[nodiscard]] consteval bool ref_from_raw_nonnull() noexcept {
    int x = 7;
    auto r = BorrowedRef<int>::from_raw_nonnull(&x);
    return r.get() == 7;
}
static_assert(ref_from_raw_nonnull());

[[nodiscard]] consteval bool ref_copy_preserves_identity() noexcept {
    int x = 100;
    BorrowedRef<int> a{x};
    BorrowedRef<int> b = a;
    return a == b && a.raw_ptr() == b.raw_ptr() && a.get() == 100;
}
static_assert(ref_copy_preserves_identity());

[[nodiscard]] consteval bool ref_two_objects_compare_distinct() noexcept {
    int x = 1;
    int y = 2;
    BorrowedRef<int> a{x};
    BorrowedRef<int> b{y};
    return !(a == b) && a.get() != b.get();
}
static_assert(ref_two_objects_compare_distinct());

// Operator-> works for class types.
struct Holder { int v = 99; };
[[nodiscard]] consteval bool ref_arrow_operator() noexcept {
    Holder h{};
    BorrowedRef<Holder> r{h};
    return r->v == 99;
}
static_assert(ref_arrow_operator());

// Default ctor is rejected.
template <class W>
concept can_default_construct = requires { W{}; };
static_assert(!can_default_construct<BorrowedRef<int>>,
    "BorrowedRef<T> default ctor MUST be deleted — null sentinel "
    "would defeat the always-bound contract.");

// ── Borrowed<T, Source> ──────────────────────────────────────────────

[[nodiscard]] consteval bool span_default_is_empty() noexcept {
    Borrowed<int, OwnerA> b{};
    return b.empty() && b.size() == 0 && b.data() == nullptr;
}
static_assert(span_default_is_empty());

[[nodiscard]] consteval bool span_binds_array() noexcept {
    int arr[4] = {1, 2, 3, 4};
    Borrowed<int, OwnerA> b{arr};
    if (b.size() != 4) return false;
    if (b[0] != 1 || b[3] != 4) return false;
    if (b.front() != 1 || b.back() != 4) return false;
    int sum = 0;
    for (int x : b) sum += x;
    return sum == 10;
}
static_assert(span_binds_array());

[[nodiscard]] consteval bool span_binds_ptr_count() noexcept {
    int arr[3] = {10, 20, 30};
    Borrowed<int, OwnerA> b{arr, 3};
    return b.size() == 3 && b[1] == 20;
}
static_assert(span_binds_ptr_count());

[[nodiscard]] consteval bool span_equality_compares_extent() noexcept {
    int arr[2] = {7, 8};
    Borrowed<int, OwnerA> a{arr};
    Borrowed<int, OwnerA> b{arr, 2};
    Borrowed<int, OwnerA> c{arr, 1};   // smaller window over same data
    return (a == b) && !(a == c);
}
static_assert(span_equality_compares_extent());

// ── subview source-preserving slice (#1090) ─────────────────────────

[[nodiscard]] consteval bool subview_extracts_window() noexcept {
    int arr[5] = {10, 20, 30, 40, 50};
    Borrowed<int, OwnerA> full{arr};
    auto window = full.subview(1, 3);   // {20, 30, 40}
    if (window.size() != 3) return false;
    if (window[0] != 20 || window[1] != 30 || window[2] != 40) return false;
    return true;
}
static_assert(subview_extracts_window());

[[nodiscard]] consteval bool subview_preserves_source() noexcept {
    int arr[3] = {1, 2, 3};
    Borrowed<int, OwnerA> b{arr};
    auto sub = b.subview(0, 2);
    // Load-bearing claim: subview's return type carries the SAME
    // Source phantom as the parent.  Cross-source assignment from
    // a subview MUST still fail.
    static_assert(std::is_same_v<decltype(sub), Borrowed<int, OwnerA>>,
        "subview MUST return Borrowed<T, Source> with the SAME Source "
        "phantom as the parent.  If this fires, the source tag has "
        "been lost — every slice operation would need re-tagging.");
    return sub.size() == 2;
}
static_assert(subview_preserves_source());

[[nodiscard]] consteval bool subview_empty_at_zero_count() noexcept {
    int arr[3] = {1, 2, 3};
    Borrowed<int, OwnerA> b{arr};
    auto empty = b.subview(0, 0);   // valid empty extraction
    return empty.size() == 0 && empty.empty();
}
static_assert(subview_empty_at_zero_count());

// ── Type-system rejections (load-bearing) ───────────────────────────

// Cross-source Borrowed is a different instantiation — assignment
// rejected.
template <class B1, class B2>
concept can_assign = requires(B1 a, B2 b) { { a = b }; };

using B_A = Borrowed<int, OwnerA>;
using B_B = Borrowed<int, OwnerB>;

static_assert(!can_assign<B_A, B_B>,
    "Borrowed<T, OwnerA> = Borrowed<T, OwnerB> MUST be a compile error. "
    "Without this rejection, a refactor that swaps two unrelated borrows "
    "would silently compile — exactly the bug class the Source phantom "
    "exists to prevent.");

// Equality across sources is also rejected — the friend operator only
// matches the same instantiation.
template <class B1, class B2>
concept can_eq = requires(B1 a, B2 b) { { a == b } -> std::convertible_to<bool>; };
static_assert(!can_eq<B_A, B_B>);

// Cross-element-type also rejected.
using B_A_int    = Borrowed<int,    OwnerA>;
using B_A_double = Borrowed<double, OwnerA>;
static_assert(!can_assign<B_A_int, B_A_double>);

// BorrowedRef cross-T also rejected.
template <class R1, class R2>
concept can_assign_ref = requires(R1 a, R2 b) { { a = b }; };
static_assert(!can_assign_ref<BorrowedRef<int>, BorrowedRef<double>>);

// Wrapper-kind diagnostic surface.
static_assert(BorrowedRef<int>::wrapper_kind() == "structural::BorrowedRef");
static_assert(B_A::wrapper_kind()              == "structural::Borrowed");

// ── Runtime smoke test ──────────────────────────────────────────────

inline void runtime_smoke_test() {
    // BorrowedRef paths.
    int x = 42;
    BorrowedRef<int> r{x};
    if (r.get() != 42) std::abort();
    if (*r != 42) std::abort();
    if (r.raw_ptr() != &x) std::abort();

    int y = 99;
    BorrowedRef<int> r2{y};
    if (r == r2) std::abort();   // distinct identities

    BorrowedRef<int> r_copy = r;
    if (!(r == r_copy)) std::abort();

    auto r_raw = BorrowedRef<int>::from_raw_nonnull(&x);
    if (r_raw.get() != 42) std::abort();

    Holder h{};
    h.v = 7;
    BorrowedRef<Holder> rh{h};
    if (rh->v != 7) std::abort();

    // Borrowed<T, Source> paths.
    Borrowed<int, OwnerA> empty{};
    if (!empty.empty()) std::abort();
    if (empty.size() != 0) std::abort();

    int arr[5] = {10, 20, 30, 40, 50};
    Borrowed<int, OwnerA> b{arr};
    if (b.size() != 5) std::abort();
    if (b.front() != 10) std::abort();
    if (b.back() != 50) std::abort();
    if (b[2] != 30) std::abort();

    int sum = 0;
    for (int v : b) sum += v;
    if (sum != 150) std::abort();

    Borrowed<int, OwnerA> b_window{arr, 2};   // first two elements only
    if (b_window.size() != 2) std::abort();
    if (b == b_window) std::abort();   // different extent

    Borrowed<int, OwnerA> b_copy = b;
    if (!(b == b_copy)) std::abort();   // same extent, same data

    // Span escape.
    auto raw_span = b.as_span();
    if (raw_span.size() != 5) std::abort();

    // subview preserves source + extracts window.
    auto window = b.subview(1, 3);
    if (window.size() != 3) std::abort();
    if (window[0] != 20 || window[2] != 40) std::abort();
    auto empty_window = b.subview(0, 0);
    if (!empty_window.empty()) std::abort();
}

}  // namespace detail::borrowed_self_test

}  // namespace crucible::safety
