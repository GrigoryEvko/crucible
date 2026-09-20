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
//
// WeakRef is the nullable member of the family: the slot starts empty,
// holds a borrowed pointer, and may be evicted.  There is no control
// block, so expiry of the referent is not detected.  The check this
// type provides is a null check and nothing more: try_get returns a
// pointer the caller has to inspect, and the unconditional accessors
// carry a non-null precondition so a missing check aborts instead of
// going quietly wrong.  Keeping the referent alive for as long as a
// WeakRef points at it remains the owner's obligation.
//
// The lifetime bound the parameter attribute announces is not enforced
// on this compiler, where the macro expands to nothing.  What the
// three types can do is refuse the one shape that always dangles: a
// borrow taken from a temporary.  Each binding constructor therefore
// has a deleted rvalue twin, so the temporary selects the deleted
// overload and the compiler names the reason.
//
// Old spelling: include/crucible/safety/Borrowed.h and
// include/crucible/safety/WeakRef.h, and the detection surfaces of
// include/crucible/safety/IsBorrowed.h and
// include/crucible/safety/IsBorrowedRef.h.

#include <foundation/Platform.h>
#include <foundation/contracts/Pre.h>
#include <foundation/reflect/Instance.h>

#include <concepts>
#include <cstddef>
#include <cstdlib>
#include <initializer_list>
#include <meta>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

namespace fixy {

namespace detail {

// "structural::" followed by the wrapper's own identifier.  Each of the
// three wrappers below spelled its name twice, once as the class and
// once inside a literal on the next line, and a rename moved only one
// of them.  Reflection reads the identifier off the class, so there is
// one spelling now.
//
// The text lives in static storage, so the view stays valid for the
// whole program.  This is the shape ChainLattice.h uses for
// pinned_at_name_v.
//
// Inside a class template body, `^^Name` is the injected class name and
// reflects the specialization, which carries no identifier of its own.
// The template does, so a specialization is asked about its template
// first.
[[nodiscard]] consteval std::string_view named_entity_of(std::meta::info cls) {
    return std::meta::has_template_arguments(cls) ? std::meta::identifier_of(std::meta::template_of(cls))
                                                  : std::meta::identifier_of(cls);
}

template <std::meta::info Cls>
[[nodiscard]] consteval std::string_view make_structural_kind() {
    std::string text{"structural::"};
    text += named_entity_of(Cls);
    return std::define_static_string(text);
}

template <std::meta::info Cls>
inline constexpr std::string_view structural_kind_v = make_structural_kind<Cls>();

}  // namespace detail

template <class T>
class [[nodiscard]] BorrowedRef {
public:
    using element_type = T;

    static constexpr std::string_view wrapper_kind() noexcept { return detail::structural_kind_v<^^BorrowedRef>; }

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

    // A const T& binds a temporary, so without this twin a BorrowedRef
    // of a prvalue compiles and dangles at the end of the statement.
    explicit BorrowedRef(T&&) = delete("BorrowedRef of a temporary dangles at the end of the full expression; "
                                       "bind the object to a name first");

    [[nodiscard]] static constexpr BorrowedRef from_raw_nonnull(T* p CRUCIBLE_LIFETIMEBOUND) noexcept
        pre(p != nullptr) {
        return BorrowedRef{from_raw_tag_t{}, p};
    }

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

    static constexpr std::string_view wrapper_kind() noexcept { return detail::structural_kind_v<^^Borrowed>; }

private:
    span_type span_{};

public:
    constexpr Borrowed() noexcept = default;

    constexpr explicit Borrowed(span_type span CRUCIBLE_LIFETIMEBOUND) noexcept : span_{span} {}

    constexpr Borrowed(T* data CRUCIBLE_LIFETIMEBOUND, std::size_t count) noexcept : span_{data, count} {}

    template <std::size_t N>
    constexpr explicit Borrowed(T (&array CRUCIBLE_LIFETIMEBOUND)[N]) noexcept : span_{array, N} {}

    // std::span of a const element type accepts an rvalue owning range
    // and a braced list, both of which die at the end of the statement,
    // so the span constructor above would take them through an implicit
    // conversion.  These two twins are the better match for each shape,
    // and both are deleted.  A span, a Borrowed and an lvalue range are
    // outside the constraint and still reach the constructors above.
    template <class R>
        requires(!std::is_lvalue_reference_v<R> && !std::same_as<std::remove_cvref_t<R>, Borrowed>
                 && std::ranges::contiguous_range<R> && !std::ranges::borrowed_range<R>)
    explicit Borrowed(R&&) = delete("Borrowed of a temporary range dangles at the end of the full expression; "
                                    "give the range a name that outlives the borrow");

    Borrowed(std::initializer_list<std::remove_cv_t<T>>) = delete(
        "Borrowed of a braced list dangles at the end of the full expression; "
        "give the elements a name that outlives the borrow");

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

template <class T>
    requires(std::is_object_v<T>)
class [[nodiscard]] WeakRef {
public:
    using element_type = T;

    static constexpr std::string_view wrapper_kind() noexcept { return detail::structural_kind_v<^^WeakRef>; }

private:
    T* ptr_ = nullptr;

    struct from_raw_tag_t {};
    constexpr WeakRef(from_raw_tag_t, T* p) noexcept : ptr_{p} {}

public:
    constexpr WeakRef() noexcept = default;

    constexpr explicit WeakRef(T& ref CRUCIBLE_LIFETIMEBOUND) noexcept : ptr_{&ref} {}

    explicit WeakRef(T&&) = delete("WeakRef of a temporary dangles at the end of the full expression; "
                                   "bind the object to a name first");

    // Null is a valid input here, so there is no precondition.
    [[nodiscard]] static constexpr WeakRef from_raw(T* p CRUCIBLE_LIFETIMEBOUND) noexcept {
        return WeakRef{from_raw_tag_t{}, p};
    }

    [[nodiscard]] constexpr bool has_value() const noexcept { return ptr_ != nullptr; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return ptr_ != nullptr; }

    [[nodiscard]] constexpr T* try_get() const noexcept { return ptr_; }

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

    constexpr void reset() noexcept { ptr_ = nullptr; }

    [[nodiscard]] friend constexpr bool operator==(WeakRef a, WeakRef b) noexcept { return a.ptr_ == b.ptr_; }
};

// The detection surface of the old IsBorrowed.h and IsBorrowedRef.h,
// plus the same question asked of WeakRef.  One reflection query
// answers each, and the associated types are read off the wrapper's
// own typedefs.  Each concept is the question; the value spelling
// beside it is derived from it and read by nothing that gates.

template <typename T>
concept IsBorrowed = ::foundation::reflect::IsInstanceOf<T, ^^Borrowed>;

template <typename T>
inline constexpr bool is_borrowed_v = IsBorrowed<T>;

template <typename T>
    requires IsBorrowed<T>
using borrowed_value_t = typename std::remove_cvref_t<T>::element_type;

template <typename T>
    requires IsBorrowed<T>
using borrowed_source_t = typename std::remove_cvref_t<T>::source_type;

template <typename T>
concept IsBorrowedRef = ::foundation::reflect::IsInstanceOf<T, ^^BorrowedRef>;

template <typename T>
inline constexpr bool is_borrowed_ref_v = IsBorrowedRef<T>;

template <typename T>
    requires IsBorrowedRef<T>
using borrowed_ref_value_t = typename std::remove_cvref_t<T>::element_type;

template <typename T>
concept IsWeakRef = ::foundation::reflect::IsInstanceOf<T, ^^WeakRef>;

template <typename T>
inline constexpr bool is_weak_ref_v = IsWeakRef<T>;

template <typename T>
    requires IsWeakRef<T>
using weak_ref_value_t = typename std::remove_cvref_t<T>::element_type;

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
static_assert(sizeof(WeakRef<int>) == sizeof(int*));
static_assert(alignof(WeakRef<int>) == alignof(int*));

static_assert(std::is_trivially_copyable_v<BorrowedRef<int>>);
static_assert(std::is_trivially_copyable_v<Borrowed<int, detail::borrowed_layout::OwnerA>>);
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
