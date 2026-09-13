#pragma once

// Non-owning views over memory a longer-lived owner holds.
//
// BorrowedRef refers to one object and is never null: there is no
// default constructor, because a null borrow would push a null check
// onto every dereference.  A caller who wants a may-be-null borrow
// wraps the borrow in an optional and says so.
//
// Borrowed views a range and carries a phantom tag naming the owner.
// The tag is what makes a view of one owner's memory a different type
// from a view of another's, so a refactor that hands the wrong borrow
// to a caller fails to compile instead of reading the wrong bytes.

#include <crucible/Platform.h>

#include <cstddef>
#include <cstdlib>
#include <span>
#include <string_view>
#include <type_traits>

namespace crucible::safety {

template <class T>
class [[nodiscard]] BorrowedRef {
public:
    using element_type = T;

    static constexpr std::string_view wrapper_kind() noexcept { return "structural::BorrowedRef"; }

private:
    // No reachable constructor leaves this initializer in play.  It is
    // here so that a constructor added later, or an aggregate path,
    // lands on a deterministic null rather than an indeterminate value.
    T* ptr_ = nullptr;

    struct from_raw_tag_t {};
    constexpr BorrowedRef(from_raw_tag_t, T* p) noexcept : ptr_{p} {}

public:
    BorrowedRef() = delete;

    constexpr explicit BorrowedRef(T& ref CRUCIBLE_LIFETIMEBOUND) noexcept : ptr_{&ref} {}

    [[nodiscard]] static constexpr BorrowedRef from_raw_nonnull(T* p CRUCIBLE_LIFETIMEBOUND) noexcept
        pre(p != nullptr) {
        return BorrowedRef{from_raw_tag_t{}, p};
    }

    constexpr BorrowedRef(BorrowedRef const&) = default;
    constexpr BorrowedRef(BorrowedRef&&) = default;
    constexpr BorrowedRef& operator=(BorrowedRef const&) = default;
    constexpr BorrowedRef& operator=(BorrowedRef&&) = default;
    ~BorrowedRef() = default;

    [[nodiscard]] constexpr T& get() const noexcept { return *ptr_; }
    [[nodiscard]] constexpr T& operator*() const noexcept { return *ptr_; }
    [[nodiscard]] constexpr T* operator->() const noexcept { return ptr_; }

    [[nodiscard]] constexpr T* raw_ptr() const noexcept { return ptr_; }

    [[nodiscard]] friend constexpr bool operator==(BorrowedRef a, BorrowedRef b) noexcept { return a.ptr_ == b.ptr_; }
};

template <class T, class Source>
class [[nodiscard]] Borrowed {
public:
    using element_type = T;
    using source_type = Source;
    using span_type = std::span<T>;

    static constexpr std::string_view wrapper_kind() noexcept { return "structural::Borrowed"; }

private:
    span_type span_{};

public:
    constexpr Borrowed() noexcept = default;

    constexpr explicit Borrowed(span_type span CRUCIBLE_LIFETIMEBOUND) noexcept : span_{span} {}

    constexpr Borrowed(T* data CRUCIBLE_LIFETIMEBOUND, std::size_t count) noexcept : span_{data, count} {}

    template <std::size_t N>
    constexpr explicit Borrowed(T (&array CRUCIBLE_LIFETIMEBOUND)[N]) noexcept : span_{array, N} {}

    constexpr Borrowed(Borrowed const&) = default;
    constexpr Borrowed(Borrowed&&) = default;
    constexpr Borrowed& operator=(Borrowed const&) = default;
    constexpr Borrowed& operator=(Borrowed&&) = default;
    ~Borrowed() = default;

    [[nodiscard]] constexpr T* data() const noexcept { return span_.data(); }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return span_.size(); }
    [[nodiscard]] constexpr bool empty() const noexcept { return span_.empty(); }
    [[nodiscard]] constexpr T* begin() const noexcept { return span_.data(); }
    [[nodiscard]] constexpr T* end() const noexcept { return span_.data() + span_.size(); }
    // These carry no contract clause.  A precondition on a constexpr
    // member forces its predicate to be a constant expression when the
    // member is called during constant evaluation, which the self-test
    // below does, and the compiler rejects that.  The index invariant
    // stands unchecked, guarded only by the standard library's own
    // debug-mode bounds overlay.
    [[nodiscard]] constexpr T& front() const noexcept { return span_.front(); }
    [[nodiscard]] constexpr T& back() const noexcept { return span_.back(); }
    [[nodiscard]] constexpr T& operator[](std::size_t i) const noexcept { return span_[i]; }

    [[nodiscard]] constexpr span_type as_span() const noexcept { return span_; }

    // Slicing carries the owner tag through.  Reaching for the bare
    // span and slicing that instead would drop the tag and produce an
    // untagged view of the same bytes.  The caller owes the invariant
    // that offset plus count stays within size, unchecked here for the
    // reason given above the element accessors.
    [[nodiscard]] constexpr Borrowed subview(std::size_t offset, std::size_t count) const noexcept {
        return Borrowed{span_.subspan(offset, count)};
    }

    [[nodiscard]] friend constexpr bool operator==(Borrowed a, Borrowed b) noexcept {
        return a.span_.data() == b.span_.data() && a.span_.size() == b.span_.size();
    }
};

namespace detail::borrowed_layout {

struct OwnerA {
    int dummy = 0;
};
struct OwnerB {
    int dummy = 0;
};

}  // namespace detail::borrowed_layout

static_assert(sizeof(BorrowedRef<int>) == sizeof(int*));
static_assert(sizeof(BorrowedRef<double>) == sizeof(double*));
static_assert(sizeof(Borrowed<int, detail::borrowed_layout::OwnerA>) == sizeof(std::span<int>));
static_assert(sizeof(Borrowed<const char, detail::borrowed_layout::OwnerA>) == sizeof(std::span<const char>));

static_assert(std::is_trivially_copyable_v<BorrowedRef<int>>);
static_assert(std::is_trivially_copyable_v<Borrowed<int, detail::borrowed_layout::OwnerA>>);
static_assert(std::is_trivially_destructible_v<BorrowedRef<int>>);
static_assert(std::is_trivially_destructible_v<Borrowed<int, detail::borrowed_layout::OwnerA>>);

static_assert(!std::is_default_constructible_v<BorrowedRef<int>>,
              "BorrowedRef<T> is not default-constructible.  A default constructor "
              "admits a null sentinel that the rest of the API does not handle.");

static_assert(std::is_default_constructible_v<Borrowed<int, detail::borrowed_layout::OwnerA>>);

namespace detail::borrowed_self_test {

using OwnerA = ::crucible::safety::detail::borrowed_layout::OwnerA;
using OwnerB = ::crucible::safety::detail::borrowed_layout::OwnerB;

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

struct Holder {
    int v = 99;
};
[[nodiscard]] consteval bool ref_arrow_operator() noexcept {
    Holder h{};
    BorrowedRef<Holder> r{h};
    return r->v == 99;
}
static_assert(ref_arrow_operator());

template <class W>
concept can_default_construct = requires { W{}; };
static_assert(!can_default_construct<BorrowedRef<int>>, "BorrowedRef<T> has a deleted default constructor.  A null "
                                                        "sentinel defeats the always-bound contract.");

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
    for (int x : b)
        sum += x;
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
    Borrowed<int, OwnerA> c{arr, 1};
    return (a == b) && !(a == c);
}
static_assert(span_equality_compares_extent());

[[nodiscard]] consteval bool subview_extracts_window() noexcept {
    int arr[5] = {10, 20, 30, 40, 50};
    Borrowed<int, OwnerA> full{arr};
    auto window = full.subview(1, 3);
    if (window.size() != 3) return false;
    if (window[0] != 20 || window[1] != 30 || window[2] != 40) return false;
    return true;
}
static_assert(subview_extracts_window());

[[nodiscard]] consteval bool subview_preserves_source() noexcept {
    int arr[3] = {1, 2, 3};
    Borrowed<int, OwnerA> b{arr};
    auto sub = b.subview(0, 2);
    static_assert(std::is_same_v<decltype(sub), Borrowed<int, OwnerA>>,
                  "subview returns Borrowed<T, Source> carrying the same Source "
                  "phantom as the parent.  Losing the tag would force every slice "
                  "site to re-tag by hand.");
    return sub.size() == 2;
}
static_assert(subview_preserves_source());

[[nodiscard]] consteval bool subview_empty_at_zero_count() noexcept {
    int arr[3] = {1, 2, 3};
    Borrowed<int, OwnerA> b{arr};
    auto empty = b.subview(0, 0);
    return empty.size() == 0 && empty.empty();
}
static_assert(subview_empty_at_zero_count());

template <class B1, class B2>
concept can_assign = requires(B1 a, B2 b) {
    { a = b };
};

using B_A = Borrowed<int, OwnerA>;
using B_B = Borrowed<int, OwnerB>;

static_assert(!can_assign<B_A, B_B>, "Borrowed<T, OwnerA> = Borrowed<T, OwnerB> is a compile error.  Without "
                                     "that rejection a refactor that swaps two unrelated borrows compiles "
                                     "silently, which is the bug class the Source phantom exists to catch.");

template <class B1, class B2>
concept can_eq = requires(B1 a, B2 b) {
    { a == b } -> std::convertible_to<bool>;
};
static_assert(!can_eq<B_A, B_B>);

using B_A_int = Borrowed<int, OwnerA>;
using B_A_double = Borrowed<double, OwnerA>;
static_assert(!can_assign<B_A_int, B_A_double>);

template <class R1, class R2>
concept can_assign_ref = requires(R1 a, R2 b) {
    { a = b };
};
static_assert(!can_assign_ref<BorrowedRef<int>, BorrowedRef<double>>);

static_assert(BorrowedRef<int>::wrapper_kind() == "structural::BorrowedRef");
static_assert(B_A::wrapper_kind() == "structural::Borrowed");

inline void runtime_smoke_test() {
    int x = 42;
    BorrowedRef<int> r{x};
    if (r.get() != 42) std::abort();
    if (*r != 42) std::abort();
    if (r.raw_ptr() != &x) std::abort();

    int y = 99;
    BorrowedRef<int> r2{y};
    if (r == r2) std::abort();

    BorrowedRef<int> r_copy = r;
    if (!(r == r_copy)) std::abort();

    auto r_raw = BorrowedRef<int>::from_raw_nonnull(&x);
    if (r_raw.get() != 42) std::abort();

    Holder h{};
    h.v = 7;
    BorrowedRef<Holder> rh{h};
    if (rh->v != 7) std::abort();

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
    for (int v : b)
        sum += v;
    if (sum != 150) std::abort();

    Borrowed<int, OwnerA> b_window{arr, 2};
    if (b_window.size() != 2) std::abort();
    if (b == b_window) std::abort();

    Borrowed<int, OwnerA> b_copy = b;
    if (!(b == b_copy)) std::abort();

    auto raw_span = b.as_span();
    if (raw_span.size() != 5) std::abort();

    auto window = b.subview(1, 3);
    if (window.size() != 3) std::abort();
    if (window[0] != 20 || window[2] != 40) std::abort();
    auto empty_window = b.subview(0, 0);
    if (!empty_window.empty()) std::abort();
}

}  // namespace detail::borrowed_self_test

}  // namespace crucible::safety
