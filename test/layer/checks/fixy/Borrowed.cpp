// The compile-time checks of fixy/Borrowed.h.

#include <fixy/Borrowed.h>

namespace fixy {

namespace detail::borrowed_layout {

struct OwnerA {
    int dummy = 0;
};
struct OwnerB {
    int dummy = 0;
};
struct brand_a {};
struct brand_b {};

using sealed_ref = BorrowedRef<int, brand_a>;
using sealed_range = Borrowed<int, OwnerA, brand_a>;

}  // namespace detail::borrowed_layout

static_assert(sizeof(BorrowedRef<int>) == sizeof(int*));
static_assert(sizeof(BorrowedRef<double>) == sizeof(double*));
static_assert(sizeof(BorrowedRef<int, detail::borrowed_layout::brand_a>) == sizeof(int*),
              "a branded borrow keeps the layout of an erased one");
static_assert(sizeof(Borrowed<int, detail::borrowed_layout::OwnerA>) == sizeof(std::span<int>));
static_assert(sizeof(Borrowed<int, detail::borrowed_layout::OwnerA, detail::borrowed_layout::brand_a>)
              == sizeof(std::span<int>));
static_assert(sizeof(Borrowed<const char, detail::borrowed_layout::OwnerA>) == sizeof(std::span<const char>));
static_assert(sizeof(WeakRef<int>) == sizeof(int*));
static_assert(alignof(WeakRef<int>) == alignof(int*));

// A borrow keeps the trivial copy that passes it in a register, and no byte
// route builds one.  A WeakRef proves nothing, so it stays trivially
// copyable.
static_assert(!std::is_trivially_copyable_v<detail::borrowed_layout::sealed_ref>
              && !::foundation::lifetime::ImplicitLifetimeThroughout<detail::borrowed_layout::sealed_ref>);
static_assert(!std::is_trivially_copyable_v<detail::borrowed_layout::sealed_range>
              && !::foundation::lifetime::ImplicitLifetimeThroughout<detail::borrowed_layout::sealed_range>);
static_assert(std::is_trivially_copy_constructible_v<detail::borrowed_layout::sealed_ref>
              && std::is_trivially_copy_constructible_v<detail::borrowed_layout::sealed_range>);
static_assert(std::is_trivially_copyable_v<WeakRef<int>>);
static_assert(std::is_trivially_destructible_v<BorrowedRef<int>>);
static_assert(std::is_trivially_destructible_v<Borrowed<int, detail::borrowed_layout::OwnerA>>);
static_assert(std::is_trivially_destructible_v<WeakRef<int>>);

static_assert(!std::is_default_constructible_v<BorrowedRef<int>>,
              "BorrowedRef<T> is not default-constructible.  A default constructor "
              "admits a null sentinel that the rest of the API does not handle.");

static_assert(std::is_default_constructible_v<Borrowed<int, detail::borrowed_layout::OwnerA>>);
static_assert(std::is_default_constructible_v<WeakRef<int>>);

static_assert(!std::is_convertible_v<WeakRef<int>, int*>);
static_assert(!std::is_convertible_v<int&, WeakRef<int>>);

// The deleted rvalue twins are what the temporary selects.  The lvalue
// forms, a span and a Borrowed stay constructible, so the twins remove
// exactly the shapes that dangle.
static_assert(!std::is_constructible_v<BorrowedRef<int>, int>);
static_assert(!std::is_constructible_v<BorrowedRef<int const>, int>);
static_assert(std::is_constructible_v<BorrowedRef<int>, int&>);
static_assert(!std::is_constructible_v<WeakRef<int>, int>);
static_assert(std::is_constructible_v<WeakRef<int>, int&>);
static_assert(!std::is_constructible_v<Borrowed<int const, detail::borrowed_layout::OwnerA>, int const (&&)[3]>);
static_assert(std::is_constructible_v<Borrowed<int const, detail::borrowed_layout::OwnerA>, std::span<int const>>);
static_assert(std::is_constructible_v<Borrowed<int, detail::borrowed_layout::OwnerA>, int (&)[3]>);
static_assert(std::is_constructible_v<Borrowed<int, detail::borrowed_layout::OwnerA>,
                                      Borrowed<int, detail::borrowed_layout::OwnerA>>);
static_assert(
    !std::is_constructible_v<Borrowed<int const, detail::borrowed_layout::OwnerA>, std::initializer_list<int>>);

// A branded borrow has no public constructor over its target: the brand
// is claimed by a mint and cannot be spelled onto another object.
static_assert(!std::is_constructible_v<BorrowedRef<int, detail::borrowed_layout::brand_a>, int&>,
              "a branded BorrowedRef is minted, never constructed, or a brand could be claimed for any object");
static_assert(!std::is_constructible_v<Borrowed<int, detail::borrowed_layout::OwnerA, detail::borrowed_layout::brand_a>,
                                       std::span<int>>,
              "a branded Borrowed is minted, never constructed");
static_assert(!std::is_constructible_v<Borrowed<int, detail::borrowed_layout::OwnerA, detail::borrowed_layout::brand_a>,
                                       int (&)[3]>);
// The erasure runs one way.
static_assert(std::is_convertible_v<BorrowedRef<int, detail::borrowed_layout::brand_a>, BorrowedRef<int>>);
static_assert(!std::is_constructible_v<BorrowedRef<int, detail::borrowed_layout::brand_a>, BorrowedRef<int>>);
static_assert(!std::is_constructible_v<BorrowedRef<int, detail::borrowed_layout::brand_a>,
                                       BorrowedRef<int, detail::borrowed_layout::brand_b>>);
static_assert(std::is_convertible_v<Borrowed<int, detail::borrowed_layout::OwnerA, detail::borrowed_layout::brand_a>,
                                    Borrowed<int, detail::borrowed_layout::OwnerA>>);
static_assert(!std::is_constructible_v<Borrowed<int, detail::borrowed_layout::OwnerA, detail::borrowed_layout::brand_a>,
                                       Borrowed<int, detail::borrowed_layout::OwnerA>>);

namespace detail::borrowed_self_test {

using OwnerA = ::fixy::detail::borrowed_layout::OwnerA;
using OwnerB = ::fixy::detail::borrowed_layout::OwnerB;

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

// The mints brand.  Two mints at two sites are two brands, a mint of a
// branded carrier takes the carrier's brand, a subview keeps its
// parent's, and a branded borrow still erases.
[[nodiscard]] consteval bool mints_brand_and_erase() noexcept {
    int arr[3] = {1, 2, 3};
    auto first = mint_borrowed<OwnerA>(arr);
    auto second = mint_borrowed<OwnerA>(arr);
    static_assert(!std::is_same_v<decltype(first), decltype(second)>, "two mint sites are two brands");
    static_assert(::foundation::brand::IsBranded<decltype(first)>);
    static_assert(std::is_same_v<borrowed_source_t<decltype(first)>, OwnerA>);
    auto sub = first.subview(1, 2);
    static_assert(::foundation::brand::SameBrand<decltype(sub), decltype(first)>, "a subview keeps the brand");
    Borrowed<int, OwnerA> erased = first;
    int x = 5;
    auto r1 = mint_borrowed_ref(x);
    auto r2 = mint_borrowed_ref(x);
    static_assert(!std::is_same_v<decltype(r1), decltype(r2)>);
    auto of_borrowed = mint_borrowed_ref(first);
    static_assert(::foundation::brand::SameBrand<decltype(of_borrowed), decltype(first)>,
                  "a borrow of a branded carrier takes the carrier's brand");
    BorrowedRef<int> erased_ref = r1;
    return erased.size() == 3 && sub[0] == 2 && *erased_ref == 5 && of_borrowed->size() == 3;
}
static_assert(mints_brand_and_erase());

// A span rvalue is a borrowed range and is admitted; an owning rvalue
// is not.
static_assert(requires(std::span<int> s) { mint_borrowed<OwnerA>(std::span<int>{s}); });

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

// The three kinds are built from the prefix and the class identifier,
// so the pins check that shape rather than restating the literal a
// fourth, fifth and sixth time.
template <class W, std::meta::info Cls>
[[nodiscard]] consteval bool kind_is_prefix_and_identifier() noexcept {
    constexpr std::string_view prefix = "structural::";
    const std::string_view kind = W::wrapper_kind();
    if (!kind.starts_with(prefix)) return false;
    return kind.substr(prefix.size()) == std::meta::identifier_of(Cls);
}

static_assert(kind_is_prefix_and_identifier<BorrowedRef<int>, ^^BorrowedRef>());
static_assert(kind_is_prefix_and_identifier<B_A, ^^Borrowed>());
static_assert(kind_is_prefix_and_identifier<WeakRef<int>, ^^WeakRef>());

// The three are distinct, which is what a caller reading the kind off a
// diagnostic relies on.
static_assert(BorrowedRef<int>::wrapper_kind() != B_A::wrapper_kind());
static_assert(B_A::wrapper_kind() != WeakRef<int>::wrapper_kind());

[[nodiscard]] consteval bool default_is_empty() noexcept {
    WeakRef<int> w{};
    return !w.has_value() && !static_cast<bool>(w) && w.try_get() == nullptr;
}
static_assert(default_is_empty());

[[nodiscard]] consteval bool binds_and_derefs() noexcept {
    int x = 42;
    WeakRef<int> w{x};
    return w.has_value() && static_cast<bool>(w) && w.try_get() == &x && w.get() == 42 && *w == 42;
}
static_assert(binds_and_derefs());

[[nodiscard]] consteval bool arrow_reaches_member() noexcept {
    struct Pair {
        int a;
        int b;
    };
    Pair p{7, 9};
    WeakRef<Pair> w{p};
    return w->a == 7 && w->b == 9;
}
static_assert(arrow_reaches_member());

[[nodiscard]] consteval bool from_raw_is_nullable() noexcept {
    WeakRef<int> empty = WeakRef<int>::from_raw(nullptr);
    int x = 5;
    WeakRef<int> full = WeakRef<int>::from_raw(&x);
    return !empty.has_value() && full.has_value() && full.try_get() == &x;
}
static_assert(from_raw_is_nullable());

[[nodiscard]] consteval bool reset_evicts() noexcept {
    int x = 1;
    WeakRef<int> w{x};
    if (!w.has_value()) return false;
    w.reset();
    return !w.has_value() && w.try_get() == nullptr;
}
static_assert(reset_evicts());

// x and y hold the same value in different objects.
[[nodiscard]] consteval bool identity_equality_and_copy() noexcept {
    int x = 3;
    int y = 3;
    WeakRef<int> a{x};
    WeakRef<int> b = a;
    WeakRef<int> c{y};
    return a == b && !(a == c) && a.try_get() == b.try_get() && WeakRef<int>{} == WeakRef<int>{};
}
static_assert(identity_equality_and_copy());

using B_dbl = Borrowed<double, OwnerA>;
using B_cc = Borrowed<const char, OwnerB>;

static_assert(is_borrowed_v<B_A>);
static_assert(is_borrowed_v<B_cc>);
static_assert(is_borrowed_v<B_dbl>);
static_assert(is_borrowed_v<B_A&>);
static_assert(is_borrowed_v<B_A const&>);
static_assert(is_borrowed_v<B_A&&>);
static_assert(!is_borrowed_v<int>);
static_assert(!is_borrowed_v<int*>);
static_assert(!is_borrowed_v<std::span<int>>,
              "A bare std::span must not satisfy is_borrowed_v. It is the underlying carrier, not "
              "the wrapper. Without this rejection, untagged spans slip through concept gates "
              "that expect a Borrowed.");
static_assert(!is_borrowed_v<B_A*>);
static_assert(!is_borrowed_v<void>);

struct LookalikeBorrowed {
    std::span<int> span_;
};
static_assert(!is_borrowed_v<LookalikeBorrowed>,
              "is_borrowed_v must reject lookalikes. If this fires, the detection "
              "has weakened to duck-typing and downstream concept overloads will misfire on user "
              "types whose member shape happens to match the internal layout of Borrowed.");

static_assert(IsBorrowed<B_A>);
static_assert(!IsBorrowed<int>);
static_assert(std::is_same_v<borrowed_value_t<B_A>, int>);
static_assert(std::is_same_v<borrowed_value_t<B_cc>, const char>);
static_assert(std::is_same_v<borrowed_source_t<B_A>, OwnerA>);
static_assert(std::is_same_v<borrowed_source_t<B_cc>, OwnerB>);
static_assert(std::is_same_v<borrowed_source_t<B_dbl>, OwnerA>);

using R_int = BorrowedRef<int>;
using R_holder = BorrowedRef<Holder>;
using R_double = BorrowedRef<double>;
using R_const = BorrowedRef<const int>;

static_assert(is_borrowed_ref_v<R_int>);
static_assert(is_borrowed_ref_v<R_holder>);
static_assert(is_borrowed_ref_v<R_double>);
static_assert(is_borrowed_ref_v<R_const>);
static_assert(is_borrowed_ref_v<R_int&>);
static_assert(is_borrowed_ref_v<R_int const&>);
static_assert(is_borrowed_ref_v<R_int&&>);
static_assert(!is_borrowed_ref_v<int>);
static_assert(!is_borrowed_ref_v<int*>,
              "A bare T* must not satisfy is_borrowed_ref_v. It is the underlying carrier, not "
              "the wrapper. Without this rejection, raw pointers slip through concept gates that "
              "expect a BorrowedRef.");
static_assert(!is_borrowed_ref_v<int&>);
static_assert(!is_borrowed_ref_v<R_int*>);
static_assert(!is_borrowed_ref_v<void>);
static_assert(!is_borrowed_ref_v<B_A>);
static_assert(!is_borrowed_ref_v<WeakRef<int>>);

struct LookalikeBorrowedRef {
    int* ptr_;
};
static_assert(!is_borrowed_ref_v<LookalikeBorrowedRef>,
              "is_borrowed_ref_v must reject lookalikes. If this fires, the detection "
              "has weakened to duck-typing and any struct holding a T* would "
              "falsely match.");

static_assert(IsBorrowedRef<R_int>);
static_assert(!IsBorrowedRef<int*>);
static_assert(std::is_same_v<borrowed_ref_value_t<R_int>, int>);
static_assert(std::is_same_v<borrowed_ref_value_t<R_holder>, Holder>);
static_assert(std::is_same_v<borrowed_ref_value_t<R_const>, const int>);

static_assert(is_weak_ref_v<WeakRef<int>>);
static_assert(is_weak_ref_v<WeakRef<Holder> const&>);
static_assert(!is_weak_ref_v<R_int>);
static_assert(!is_weak_ref_v<int*>);
static_assert(IsWeakRef<WeakRef<int>&&>);
static_assert(std::is_same_v<weak_ref_value_t<WeakRef<Holder>>, Holder>);

}  // namespace detail::borrowed_self_test

}  // namespace fixy
