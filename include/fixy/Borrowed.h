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
// Both carry a brand, which is the identity of the one borrow event or
// of the branded object the borrow was taken from.  A borrow minted by
// mint_borrowed or mint_borrowed_ref carries a fresh brand, or the
// brand of a carrier that has one, and a callee that wants two borrows
// of the same thing asks for one brand on both.  The constructors are
// the erased doors: a borrow built through one names no brand and is
// interchangeable with every other erased borrow of its type.  A
// branded borrow has no public constructor, so a brand cannot be
// claimed for a different object by spelling it.  foundation/Brand.h
// states the facts a brand rests on.
//
// WeakRef is the nullable member of the family: the slot starts empty,
// holds a borrowed pointer, and may be evicted.  There is no control
// block, so expiry of the referent is not detected.  The check this
// type provides is a null check and nothing more: try_get returns a
// pointer the caller has to inspect, and the unconditional accessors
// carry a non-null precondition so a missing check aborts instead of
// going quietly wrong.  Keeping the referent alive for as long as a
// WeakRef points at it remains the owner's obligation.  It carries no
// brand: a nullable slot has no one identity to carry.
//
// The lifetime bound the parameter attribute announces is not enforced
// on this compiler, where the macro expands to nothing.  What the
// three types can do is refuse the one shape that always dangles: a
// borrow taken from a temporary.  Each binding constructor and each
// mint therefore has a deleted rvalue twin, so the temporary selects
// the deleted overload and the compiler names the reason.

#include <foundation/Brand.h>
#include <foundation/Lifetime.h>
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

// "structural::" followed by the wrapper's own identifier.  Reflection
// reads the identifier off the class, so the name has one spelling.  A
// literal beside the class would be a second spelling, and a rename can
// move only one of the two.
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

// The one door to a branded borrow.  Only the mints hold it.
struct borrow_mint_t {};

}  // namespace detail

// The claims the carriers in this header make that no lattice grades.
// foundation/diag/RowHash.h folds each identity, so every carrier here
// takes a cache slot of its own rather than the zero a bare payload has.
namespace row_discipline {
struct borrowed_ref;
struct borrowed;
struct weak_ref;
}  // namespace row_discipline

template <class T, class Brand = ::foundation::brand::DefaultBrand>
class BorrowedRef;

template <class T, class Source, class Brand = ::foundation::brand::DefaultBrand>
class Borrowed;

// Declared here so that Borrowed can befriend the borrow of a region,
// which OwnedRegion.h defines.  The defaulted brand is on the
// declaration there.
template <class T, class Tag, class Brand>
class OwnedRegion;

template <class T, class Tag, class Brand>
    requires ::foundation::brand::IsBrand<Brand>
[[nodiscard]] constexpr Borrowed<T, Tag, Brand> mint_borrowed(OwnedRegion<T, Tag, Brand>& region
                                                              CRUCIBLE_LIFETIMEBOUND) noexcept;

// The detection surface of Borrowed and BorrowedRef.  One reflection
// query answers each, and the associated types are read off the
// wrapper's own typedefs.  Each concept is the question.  The value
// spelling beside it is derived from it, and nothing that gates reads
// it.  They are declared ahead of the classes because the deleted
// rvalue twins below exclude a Borrowed from the ranges they refuse.

template <typename T>
concept IsBorrowed = ::foundation::reflect::IsInstanceOf<T, ^^Borrowed>;

template <typename T>
inline constexpr bool is_borrowed_v = IsBorrowed<T>;

template <typename T>
concept IsBorrowedRef = ::foundation::reflect::IsInstanceOf<T, ^^BorrowedRef>;

template <typename T>
inline constexpr bool is_borrowed_ref_v = IsBorrowedRef<T>;

// The element type of a borrow taken from a range: the range's
// reference type with the reference removed, so a const range yields a
// borrow of const elements.
template <class R>
using range_element_t = std::remove_reference_t<std::ranges::range_reference_t<R>>;

// A branded borrow of one object.  The brand is the carrier's own when
// the carrier has one, so the borrow is pinned to that instance, and a
// fresh one otherwise.
template <class T, class Fresh = CRUCIBLE_FRESH_BRAND>
    requires std::is_object_v<T>
[[nodiscard]] constexpr BorrowedRef<T, ::foundation::brand::inherited_or_fresh_brand_t<T, Fresh>>
mint_borrowed_ref(T& ref CRUCIBLE_LIFETIMEBOUND) noexcept;

// A borrow of a temporary dangles at the end of the statement.  The
// forwarding reference is the better match for an rvalue, and it is
// constrained away from lvalues so a named object still reaches the
// mint above.
template <class T, class Fresh = CRUCIBLE_FRESH_BRAND>
    requires(!std::is_lvalue_reference_v<T>)
constexpr auto mint_borrowed_ref(T&&) = delete("a borrow of a temporary dangles at the end of the full expression; "
                                               "bind the object to a name first");

// A branded borrow of a contiguous range, tagged with its owner.  The
// range is taken by lvalue reference, or by rvalue when it is a
// borrowed range whose elements outlive it, such as a span.  Every
// other rvalue selects the deleted twin.
template <class Source, class R, class Fresh = CRUCIBLE_FRESH_BRAND>
    requires(std::is_lvalue_reference_v<R> && std::ranges::contiguous_range<R> && !IsBorrowed<R>)
[[nodiscard]] constexpr Borrowed<range_element_t<R>, Source, Fresh> mint_borrowed(R&& range
                                                                                  CRUCIBLE_LIFETIMEBOUND) noexcept;

template <class Source, class R, class Fresh = CRUCIBLE_FRESH_BRAND>
    requires(!std::is_lvalue_reference_v<R> && std::ranges::contiguous_range<R> && std::ranges::borrowed_range<R>
             && !IsBorrowed<R>)
[[nodiscard]] constexpr Borrowed<range_element_t<R>, Source, Fresh> mint_borrowed(R&& range) noexcept;

template <class Source, class R, class Fresh = CRUCIBLE_FRESH_BRAND>
    requires(!std::is_lvalue_reference_v<R> && std::ranges::contiguous_range<R> && !std::ranges::borrowed_range<R>
             && !IsBorrowed<R>)
constexpr auto mint_borrowed(R&&) = delete("a borrow of a temporary range dangles at the end of the full expression; "
                                           "give the range a name that outlives the borrow");

template <class T, class Brand>
class [[nodiscard]] BorrowedRef {
    static_assert(::foundation::brand::IsBrand<Brand>, "BorrowedRef<T, Brand>: Brand must be an empty class type: "
                                                       "a mint's fresh brand, the carrier's own, or DefaultBrand.");

public:
    using element_type = T;
    using brand_type = Brand;

    static constexpr std::string_view wrapper_kind() noexcept { return detail::structural_kind_v<^^BorrowedRef>; }
    using row_discipline = ::fixy::row_discipline::borrowed_ref;
    using row_payload = T;

private:
    // No reachable constructor leaves this initializer in play.  It is
    // here so that a constructor added later, or an aggregate path,
    // lands on a deterministic null rather than an indeterminate value.
    T* ptr_ = nullptr;
    // No byte route builds a borrow, so a branded borrow names only the
    // object that its mint took.
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    struct from_raw_tag_t {};
    constexpr BorrowedRef(from_raw_tag_t, T* p) noexcept : ptr_{p} {}

    // The branded door.  Only the mint holds the key, so a brand cannot
    // be claimed for a second object by spelling it.
    constexpr BorrowedRef(detail::borrow_mint_t, T& ref) noexcept : ptr_{&ref} {}

    template <class U, class Fresh>
        requires std::is_object_v<U>
    friend constexpr BorrowedRef<U, ::foundation::brand::inherited_or_fresh_brand_t<U, Fresh>>
    mint_borrowed_ref(U& ref CRUCIBLE_LIFETIMEBOUND) noexcept;

public:
    BorrowedRef() = delete;

    // The erased door.  A borrow built here names no brand.
    constexpr explicit BorrowedRef(T& ref CRUCIBLE_LIFETIMEBOUND) noexcept
        requires std::is_same_v<Brand, ::foundation::brand::DefaultBrand>
        : ptr_{&ref} {}

    // A const T& binds a temporary, so without this twin a BorrowedRef
    // of a prvalue compiles and dangles at the end of the statement.
    explicit BorrowedRef(T&&) = delete("BorrowedRef of a temporary dangles at the end of the full expression; "
                                       "bind the object to a name first");

    // Erasure, one way only: a borrow of one instance becomes a borrow
    // on the erased identity.  Nothing gives an erased borrow a brand.
    template <class Other>
        requires(std::is_same_v<Brand, ::foundation::brand::DefaultBrand> && ::foundation::brand::IsFreshBrand<Other>)
    constexpr BorrowedRef(BorrowedRef<T, Other> const& other) noexcept : ptr_{other.raw_ptr()} {}

    // Unenforceable by construction, and that is why the annotation is
    // absent rather than decorative.  The hazard is the POINTEE's
    // lifetime, and a pointer parameter cannot see it: every argument
    // is a prvalue, so no deleted rvalue twin can tell a pointer to a
    // live object from a pointer to a dead one.  The constructor above
    // takes T& and has its twin, and that is the door to use whenever
    // the caller has an object rather than an address.  This factory is
    // the address door, for a pointer that arrived from C, and the
    // caller owns the lifetime argument.  It is erased, because an
    // address carries no identity a brand could name.
    [[nodiscard]] static constexpr BorrowedRef from_raw_nonnull(T* p) noexcept
        requires std::is_same_v<Brand, ::foundation::brand::DefaultBrand>
    {
        CRUCIBLE_PRE(p != nullptr);
        return BorrowedRef{from_raw_tag_t{}, p};
    }

    [[nodiscard]] constexpr T& get() const noexcept { return *ptr_; }
    [[nodiscard]] constexpr T& operator*() const noexcept { return *ptr_; }
    [[nodiscard]] constexpr T* operator->() const noexcept { return ptr_; }

    [[nodiscard]] constexpr T* raw_ptr() const noexcept { return ptr_; }

    [[nodiscard]] friend constexpr bool operator==(BorrowedRef a, BorrowedRef b) noexcept { return a.ptr_ == b.ptr_; }
};

template <class T, class Source, class Brand>
class [[nodiscard]] Borrowed {
    static_assert(::foundation::brand::IsBrand<Brand>, "Borrowed<T, Source, Brand>: Brand must be an empty class "
                                                       "type: a mint's fresh brand, the region's own, or "
                                                       "DefaultBrand.");

public:
    using element_type = T;
    using source_type = Source;
    using brand_type = Brand;
    using span_type = std::span<T>;
    using row_discipline = ::fixy::row_discipline::borrowed;
    using row_payload = T;

    static constexpr std::string_view wrapper_kind() noexcept { return detail::structural_kind_v<^^Borrowed>; }

private:
    span_type span_{};
    // No byte route builds a borrow, so a branded borrow names only the
    // range that its mint took.
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    // The branded door, for the mints and for subview.
    constexpr Borrowed(detail::borrow_mint_t, span_type span) noexcept : span_{span} {}

    template <class USource, class R, class Fresh>
        requires(std::is_lvalue_reference_v<R> && std::ranges::contiguous_range<R> && !IsBorrowed<R>)
    friend constexpr Borrowed<range_element_t<R>, USource, Fresh> mint_borrowed(R&& range
                                                                                CRUCIBLE_LIFETIMEBOUND) noexcept;

    template <class USource, class R, class Fresh>
        requires(!std::is_lvalue_reference_v<R> && std::ranges::contiguous_range<R> && std::ranges::borrowed_range<R>
                 && !IsBorrowed<R>)
    friend constexpr Borrowed<range_element_t<R>, USource, Fresh> mint_borrowed(R&& range) noexcept;

    // A borrow of a branded region is minted in OwnedRegion.h and
    // carries the region's brand, so it needs this door too.
    template <class U, class UTag, class UBrand>
        requires ::foundation::brand::IsBrand<UBrand>
    friend constexpr Borrowed<U, UTag, UBrand> mint_borrowed(OwnedRegion<U, UTag, UBrand>& region
                                                             CRUCIBLE_LIFETIMEBOUND) noexcept;

public:
    constexpr Borrowed() noexcept = default;

    // The erased doors.  A borrow built through one names no brand.
    constexpr explicit Borrowed(span_type span CRUCIBLE_LIFETIMEBOUND) noexcept
        requires std::is_same_v<Brand, ::foundation::brand::DefaultBrand>
        : span_{span} {}

    // Unenforceable by construction, like BorrowedRef::from_raw_nonnull:
    // the hazard is the lifetime of what data points at, every pointer
    // argument is a prvalue, and no overload can separate the two cases.
    // The span and array constructors above carry their twins; this is
    // the pointer-and-count door for a C boundary.
    constexpr Borrowed(T* data, std::size_t count) noexcept
        requires std::is_same_v<Brand, ::foundation::brand::DefaultBrand>
        : span_{data, count} {}

    template <std::size_t N>
    constexpr explicit Borrowed(T (&array CRUCIBLE_LIFETIMEBOUND)[N]) noexcept
        requires std::is_same_v<Brand, ::foundation::brand::DefaultBrand>
        : span_{array, N} {}

    // std::span of a const element type accepts an rvalue owning range
    // and a braced list, both of which die at the end of the statement,
    // so the span constructor above would take them through an implicit
    // conversion.  These two twins are the better match for each shape,
    // and both are deleted.  A span, a Borrowed and an lvalue range are
    // outside the constraint and still reach the constructors above.
    template <class R>
        requires(!std::is_lvalue_reference_v<R> && !IsBorrowed<R> && std::ranges::contiguous_range<R>
                 && !std::ranges::borrowed_range<R>)
    explicit Borrowed(R&&) = delete("Borrowed of a temporary range dangles at the end of the full expression; "
                                    "give the range a name that outlives the borrow");

    Borrowed(std::initializer_list<std::remove_cv_t<T>>) = delete(
        "Borrowed of a braced list dangles at the end of the full expression; "
        "give the elements a name that outlives the borrow");

    // Erasure, one way only, as on BorrowedRef.
    template <class Other>
        requires(std::is_same_v<Brand, ::foundation::brand::DefaultBrand> && ::foundation::brand::IsFreshBrand<Other>)
    constexpr Borrowed(Borrowed<T, Source, Other> const& other) noexcept : span_{other.as_span()} {}

    [[nodiscard]] constexpr T* data() const noexcept { return span_.data(); }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return span_.size(); }
    [[nodiscard]] constexpr bool empty() const noexcept { return span_.empty(); }
    [[nodiscard]] constexpr T* begin() const noexcept { return span_.data(); }
    [[nodiscard]] constexpr T* end() const noexcept { return span_.data() + span_.size(); }
    // These carry no contract clause.  A precondition on a constexpr
    // member forces its predicate to be a constant expression when the
    // member is called during constant evaluation, which the check file
    // of this header does, and the compiler rejects that.  The index invariant
    // stands unchecked, guarded only by the standard library's own
    // debug-mode bounds overlay.
    [[nodiscard]] constexpr T& front() const noexcept { return span_.front(); }
    [[nodiscard]] constexpr T& back() const noexcept { return span_.back(); }
    [[nodiscard]] constexpr T& operator[](std::size_t i) const noexcept { return span_[i]; }

    [[nodiscard]] constexpr span_type as_span() const noexcept { return span_; }

    // Slicing carries the owner tag and the brand through.  Reaching
    // for the bare span and slicing that instead would drop both and
    // produce an untagged view of the same bytes.  The caller owes the
    // invariant that offset plus count stays within size, unchecked
    // here for the reason given above the element accessors.
    [[nodiscard]] constexpr Borrowed subview(std::size_t offset, std::size_t count) const noexcept {
        return Borrowed{detail::borrow_mint_t{}, span_.subspan(offset, count)};
    }

    [[nodiscard]] friend constexpr bool operator==(Borrowed a, Borrowed b) noexcept {
        return a.span_.data() == b.span_.data() && a.span_.size() == b.span_.size();
    }
};

template <class T, class Fresh>
    requires std::is_object_v<T>
[[nodiscard]] constexpr BorrowedRef<T, ::foundation::brand::inherited_or_fresh_brand_t<T, Fresh>>
mint_borrowed_ref(T& ref CRUCIBLE_LIFETIMEBOUND) noexcept {
    return BorrowedRef<T, ::foundation::brand::inherited_or_fresh_brand_t<T, Fresh>>{detail::borrow_mint_t{}, ref};
}

template <class Source, class R, class Fresh>
    requires(std::is_lvalue_reference_v<R> && std::ranges::contiguous_range<R> && !IsBorrowed<R>)
[[nodiscard]] constexpr Borrowed<range_element_t<R>, Source, Fresh> mint_borrowed(R&& range
                                                                                  CRUCIBLE_LIFETIMEBOUND) noexcept {
    return Borrowed<range_element_t<R>, Source, Fresh>{
        detail::borrow_mint_t{}, std::span<range_element_t<R>>{std::ranges::data(range), std::ranges::size(range)}};
}

template <class Source, class R, class Fresh>
    requires(!std::is_lvalue_reference_v<R> && std::ranges::contiguous_range<R> && std::ranges::borrowed_range<R>
             && !IsBorrowed<R>)
[[nodiscard]] constexpr Borrowed<range_element_t<R>, Source, Fresh> mint_borrowed(R&& range) noexcept {
    return Borrowed<range_element_t<R>, Source, Fresh>{
        detail::borrow_mint_t{}, std::span<range_element_t<R>>{std::ranges::data(range), std::ranges::size(range)}};
}

template <class T>
    requires(std::is_object_v<T>)
class [[nodiscard]] WeakRef {
public:
    using element_type = T;

    static constexpr std::string_view wrapper_kind() noexcept { return detail::structural_kind_v<^^WeakRef>; }
    using row_discipline = ::fixy::row_discipline::weak_ref;
    using row_payload = T;

private:
    T* ptr_ = nullptr;

    struct from_raw_tag_t {};
    constexpr WeakRef(from_raw_tag_t, T* p) noexcept : ptr_{p} {}

public:
    constexpr WeakRef() noexcept = default;

    constexpr explicit WeakRef(T& ref CRUCIBLE_LIFETIMEBOUND) noexcept : ptr_{&ref} {}

    explicit WeakRef(T&&) = delete("WeakRef of a temporary dangles at the end of the full expression; "
                                   "bind the object to a name first");

    // Null is a valid input here, so there is no precondition.  The
    // lifetime claim is unenforceable for the same reason as
    // BorrowedRef::from_raw_nonnull, so it is stated here rather than
    // annotated: a pointer parameter cannot tell a live pointee from a
    // dead one, and the T& constructor above is the door that can.
    [[nodiscard]] static constexpr WeakRef from_raw(T* p) noexcept { return WeakRef{from_raw_tag_t{}, p}; }

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

template <typename T>
    requires IsBorrowed<T>
using borrowed_value_t = typename std::remove_cvref_t<T>::element_type;

template <typename T>
    requires IsBorrowed<T>
using borrowed_source_t = typename std::remove_cvref_t<T>::source_type;

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

}  // namespace fixy
