#pragma once

// The branded scope of the Region family: proven indexes for a View whose
// count no type holds.
//
//   mint_branded_scope(view, body)
//                       calls body with the BrandedView of view, which has the
//                       fresh brand of the instance of the mint, and gives the
//                       result of body
//   BrandedView<T, Brand>
//                       the View of one scope.  Its count never changes, so an
//                       index that it proves stays below its count
//   BrandedIndex<Brand> an index of 4 bytes below the count of the BrandedView
//                       of Brand.  Only that view makes one: index(raw) gives
//                       an Option, and indices() and enumerate() give each one
//
// THE OPERATIONS OF A BRANDED VIEW
//   size(), view(), for (x : view)
//   index(raw)          Option<BrandedIndex<Brand>>: one compare
//   indices()           each BrandedIndex<Brand>, in order
//   at(index)           the element at a BrandedIndex<Brand>, with no check
//   enumerate()         each element with its BrandedIndex: a Record of the
//                       index and a Ref with the brand of the scope
//   parallel(other)     Option<BrandedView<U, Brand>>: the View other with the
//                       brand of the scope when its count is the same, one
//                       compare.  Each index of the scope then reads each
//                       parallel array with no check
//
// The result of the body must not hold the brand: a BrandedIndex or a Ref of
// the scope outside it would prove nothing about a later scope.  The mint
// refuses a result type that names the brand at each depth of its template
// arguments, and the body must give a value, because the mint is
// [[nodiscard]].  A View of more than 0xFFFFFFFF elements ends the process,
// because an index of 4 bytes cannot prove a larger count.
//
// A brand belongs to an instance of the mint, not to one call.  A loop or a
// pointer to the mint calls one instance again, so a static local of the
// body could keep an index for a later scope of fewer elements, and at() has
// no check.  The brand is a scope brand (CRUCIBLE_SCOPE_BRAND of
// foundation/Brand.h), so the rule brand-escape of the quarantine plugin
// makes each object of static or thread storage duration that holds it a
// compile error, and each result of the mint that holds it in a capture or a
// field (neg_branded_scope_static_local, neg_branded_scope_returns_closure).

#include <foundation/Brand.h>
#include <foundation/ByteSeal.h>
#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Record.h>
#include <foundation/core/Ref.h>
#include <foundation/core/Region.h>
#include <foundation/core/Report.h>

#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <type_traits>

namespace foundation::core {

template <::foundation::brand::IsBrand Brand>
class BrandedIndex;

template <class T, ::foundation::brand::IsBrand Brand>
class BrandedView;

namespace detail {

// The largest count that a BrandedIndex proves an index below.
inline constexpr std::size_t max_branded_count = 0xFFFFFFFFu;

template <::foundation::brand::IsBrand Brand>
class BrandedIndexRange;
template <class Element, ::foundation::brand::IsBrand Brand>
class BrandedEnumerateRange;
template <class Element, ::foundation::brand::IsBrand Brand>
class BrandedEnumerateCursor;

// The door of mint_branded_scope to the private constructor of a
// BrandedView.  Only the mint calls it.
struct BrandedScopeDoor {
    template <::foundation::brand::IsBrand Brand, class T>
    [[nodiscard]] static constexpr BrandedView<T, Brand> open(T* first, std::size_t count) noexcept {
        return BrandedView<T, Brand>{first, static_cast<std::uint32_t>(count)};
    }
};

// The template arguments of a type, by index.  The walk indexes the list, so
// it makes no static list.
[[nodiscard]] consteval std::size_t template_argument_count(std::meta::info type) {
    return std::meta::template_arguments_of(type).size();
}

[[nodiscard]] consteval std::meta::info template_argument_at(std::meta::info type, std::size_t index) {
    return std::meta::template_arguments_of(type)[index];
}

// True when the type is the brand, or names the brand at a depth of its
// template arguments.  The walk reads each template argument that is a
// type.  The function is not a template, so no unit can specialize it and
// change what the gate admits.  Complexity: O(A^2) for A template arguments
// at one depth, because each index reads the list again, at compile time
// only.
[[nodiscard]] consteval bool names_brand(std::meta::info type, std::meta::info brand) {
    std::meta::info const bare = std::meta::dealias(std::meta::remove_cvref(type));
    if (bare == brand) {
        return true;
    }
    if (!std::meta::has_template_arguments(bare)) {
        return false;
    }
    std::size_t const count = template_argument_count(bare);
    for (std::size_t index = 0; index < count; ++index) {
        std::meta::info const argument = template_argument_at(bare, index);
        if (std::meta::is_type(argument) && names_brand(argument, brand)) {
            return true;
        }
    }
    return false;
}

}  // namespace detail

// An index below the count of the BrandedView of Brand.  The value
// 0xFFFFFFFF is no index, because no branded count is above it, so an
// Option<BrandedIndex<Brand>> has 4 bytes.
template <::foundation::brand::IsBrand Brand>
class BrandedIndex final {
    std::uint32_t value_ = 0;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    template <class T, ::foundation::brand::IsBrand OtherBrand>
    friend class BrandedView;
    friend class detail::BrandedIndexRange<Brand>;
    template <class Element, ::foundation::brand::IsBrand OtherBrand>
    friend class detail::BrandedEnumerateCursor;
    friend struct niche<BrandedIndex>;

    constexpr explicit BrandedIndex(std::uint32_t value) noexcept : value_{value} {}

public:
    // The number, which is below the count of the view of Brand.
    [[nodiscard]] constexpr std::size_t raw() const noexcept { return value_; }

    [[nodiscard]] friend constexpr bool operator==(BrandedIndex left, BrandedIndex right) noexcept {
        return left.value_ == right.value_;
    }
    [[nodiscard]] friend constexpr std::strong_ordering operator<=>(BrandedIndex left, BrandedIndex right) noexcept {
        return left.value_ <=> right.value_;
    }
};

// The empty value of an Option of a BrandedIndex: 0xFFFFFFFF, which no index
// holds.
template <::foundation::brand::IsBrand Brand>
struct niche<BrandedIndex<Brand>> {
    [[nodiscard]] static constexpr BrandedIndex<Brand> empty(niche_key<BrandedIndex<Brand>> const&) noexcept {
        return BrandedIndex<Brand>{0xFFFFFFFFu};
    }
    [[nodiscard]] static constexpr bool is_empty(niche_key<BrandedIndex<Brand>> const&,
                                                 BrandedIndex<Brand> const& index) noexcept {
        return index.value_ == 0xFFFFFFFFu;
    }
};

namespace detail {

// The position of a loop over the indexes of a branded view: the next index
// and the count.  A read or a step at the end ends the process in each
// build.  The position starts at or before the count, and a step at the count
// ends the process, so the position never passes the count.  Thus each check
// is the compare at_ == end_, the same compare as the test of the loop, and
// the optimizer removes the two checks in a loop that compares the cursor
// with the end first.
template <::foundation::brand::IsBrand Brand>
class BrandedIndexCursor {
    std::size_t at_ = 0;
    std::size_t end_ = 0;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    friend class BrandedIndexRange<Brand>;

    constexpr BrandedIndexCursor(std::size_t at, std::size_t end) noexcept : at_{at}, end_{end} {}

public:
    [[nodiscard]] constexpr BrandedIndex<Brand> operator*() const noexcept {
        if (at_ == end_) [[unlikely]] {
            fatal("a read through the end cursor of a loop over the indexes of a branded view");
        }
        return BrandedIndexRange<Brand>::index_at_(at_);
    }

    constexpr BrandedIndexCursor& operator++() noexcept {
        if (at_ == end_) [[unlikely]] {
            fatal("a step past the end cursor of a loop over the indexes of a branded view");
        }
        ++at_;
        return *this;
    }

    [[nodiscard]] friend constexpr bool operator==(BrandedIndexCursor const& left,
                                                   BrandedIndexCursor const& right) noexcept {
        return left.at_ == right.at_;
    }
};

// Each index of a branded view, in order.
template <::foundation::brand::IsBrand Brand>
class BrandedIndexRange {
    std::size_t count_ = 0;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    template <class T, ::foundation::brand::IsBrand OtherBrand>
    friend class ::foundation::core::BrandedView;
    friend class BrandedIndexCursor<Brand>;

    constexpr explicit BrandedIndexRange(std::size_t count) noexcept : count_{count} {}

    // The index at a position below the count of the view.
    [[nodiscard]] static constexpr BrandedIndex<Brand> index_at_(std::size_t position) noexcept {
        return BrandedIndex<Brand>{static_cast<std::uint32_t>(position)};
    }

public:
    [[nodiscard]] constexpr BrandedIndexCursor<Brand> begin() const noexcept {
        return BrandedIndexCursor<Brand>{0, count_};
    }
    [[nodiscard]] constexpr BrandedIndexCursor<Brand> end() const noexcept {
        return BrandedIndexCursor<Brand>{count_, count_};
    }
};

// The position of a loop over the elements of a branded view with their
// indexes.  The position is a full word, so it is also the offset of the
// element, and the index of each step comes from it after its compare.  As
// for BrandedIndexCursor, the position never passes the count, so each check
// is the compare at_ == end_.
template <class Element, ::foundation::brand::IsBrand Brand>
class BrandedEnumerateCursor {
    Element* first_ = nullptr;
    std::size_t at_ = 0;
    std::size_t end_ = 0;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    friend class BrandedEnumerateRange<Element, Brand>;

    constexpr BrandedEnumerateCursor(Element* first, std::size_t at, std::size_t end) noexcept
        : first_{first}, at_{at}, end_{end} {}

public:
    [[nodiscard]] constexpr Record<BrandedIndex<Brand>, Ref<Element, Brand>> operator*() const noexcept {
        if (at_ == end_) [[unlikely]] {
            fatal("a read through the end cursor of an enumerate loop over a branded view");
        }
        return {BrandedIndex<Brand>{static_cast<std::uint32_t>(at_)}, RefDoor::lend<Brand>(first_[at_])};
    }

    constexpr BrandedEnumerateCursor& operator++() noexcept {
        if (at_ == end_) [[unlikely]] {
            fatal("a step past the end cursor of an enumerate loop over a branded view");
        }
        ++at_;
        return *this;
    }

    [[nodiscard]] friend constexpr bool operator==(BrandedEnumerateCursor const& left,
                                                   BrandedEnumerateCursor const& right) noexcept {
        return left.at_ == right.at_;
    }
};

// The elements of a branded view, each with its index.
template <class Element, ::foundation::brand::IsBrand Brand>
class BrandedEnumerateRange {
    Element* first_ = nullptr;
    std::size_t count_ = 0;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    template <class T, ::foundation::brand::IsBrand OtherBrand>
    friend class ::foundation::core::BrandedView;

    constexpr BrandedEnumerateRange(Element* first, std::size_t count) noexcept : first_{first}, count_{count} {}

public:
    [[nodiscard]] constexpr BrandedEnumerateCursor<Element, Brand> begin() const noexcept {
        return BrandedEnumerateCursor<Element, Brand>{first_, 0, count_};
    }
    [[nodiscard]] constexpr BrandedEnumerateCursor<Element, Brand> end() const noexcept {
        return BrandedEnumerateCursor<Element, Brand>{first_, count_, count_};
    }
};

}  // namespace detail

// The View of one branded scope.  It comes only from mint_branded_scope,
// and its count never changes, so each BrandedIndex<Brand> stays below it.
template <class T, ::foundation::brand::IsBrand Brand>
class BrandedView final {
    T* first_ = nullptr;
    std::uint32_t count_ = 0;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    friend struct detail::BrandedScopeDoor;

    constexpr BrandedView(T* first, std::uint32_t count) noexcept : first_{first}, count_{count} {}

public:
    [[nodiscard]] constexpr std::size_t size() const noexcept { return count_; }

    // The plain View of the elements.  It has no brand, so an index of the
    // scope proves nothing about it.
    [[nodiscard]] constexpr View<T> view() const noexcept { return detail::view_over_(first_, size()); }

    [[nodiscard]] constexpr ViewCursor<T> begin() const noexcept { return view().begin(); }
    [[nodiscard]] constexpr ViewCursor<T> end() const noexcept { return view().end(); }

    // The index raw of this view, or no value when raw is not below its
    // count.  One compare.
    [[nodiscard]] constexpr Option<BrandedIndex<Brand>> index(std::size_t raw) const noexcept {
        if (raw >= size()) {
            return Option<BrandedIndex<Brand>>{none};
        }
        return Option<BrandedIndex<Brand>>::some(BrandedIndex<Brand>{static_cast<std::uint32_t>(raw)});
    }

    // Each index of this view, in order: the loop
    // `for (BrandedIndex<Brand> index : view.indices())`.
    [[nodiscard]] constexpr detail::BrandedIndexRange<Brand> indices() const noexcept {
        return detail::BrandedIndexRange<Brand>{size()};
    }

    // The element at an index of this view.  The brand proves the index below
    // the count, so the read has no check.
    [[nodiscard]] constexpr T& at(BrandedIndex<Brand> index) const noexcept {
        std::size_t const position = index.raw();
        [[assume(position < count_)]];
        return first_[position];
    }

    // The loop `for (auto [index, element] : view.enumerate())`: each element
    // with its index, and a Ref with the brand of the scope.
    [[nodiscard]] constexpr detail::BrandedEnumerateRange<T, Brand> enumerate() const noexcept {
        return detail::BrandedEnumerateRange<T, Brand>{first_, size()};
    }

    // The BrandedView of other with the brand of this scope, or no value when
    // other has a different count: one compare.  The two counts are equal and
    // never change, so each index of the scope is below the count of the
    // result too, and at() of the result has no check.  The parallel arrays
    // of one run, such as the columns of a table, then share one index.
    template <class U, std::size_t Extent>
    [[nodiscard]] constexpr Option<BrandedView<U, Brand>> parallel(View<U, Extent> other) const noexcept {
        if (other.size() != size()) {
            return Option<BrandedView<U, Brand>>{none};
        }
        U* const first = count_ == 0 ? nullptr : first_of_(detail::ViewDoor::key(), other);
        return Option<BrandedView<U, Brand>>::some(detail::BrandedScopeDoor::open<Brand>(first, count_));
    }
};

// The arguments of mint_branded_scope: a body that takes the BrandedView of
// the scope with no throw and gives a value that does not hold the brand.
template <class T, class Body, class Brand>
concept CanMintBrandedScope =
    ::foundation::brand::IsBrand<Brand> && std::is_nothrow_invocable_v<Body&&, BrandedView<T, Brand>>
    && !std::is_void_v<std::invoke_result_t<Body&&, BrandedView<T, Brand>>>
    && !detail::names_brand(^^std::invoke_result_t<Body&&, BrandedView<T, Brand>>, ^^Brand);

// Calls body with the BrandedView of view, with the fresh brand of the
// instance of the mint, and gives the result of body.  A View of more than 0xFFFFFFFF elements
// ends the process.  The pack refuses each explicit argument, so no caller
// names the brand.
template <class... Refused, class T, std::size_t Extent, class Body,
          ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
    requires(sizeof...(Refused) == 0 && CanMintBrandedScope<T, Body, Fresh>)
[[nodiscard]] constexpr auto mint_branded_scope(View<T, Extent> view, Body&& body) noexcept {
    std::size_t const count = view.size();
    if (count > detail::max_branded_count) [[unlikely]] {
        fatal("a branded scope over a View of {} elements, past the count that an index of 4 bytes proves", count);
    }
    T* const first = count == 0 ? nullptr : first_of_(detail::ViewDoor::key(), view);
    return static_cast<Body&&>(body)(detail::BrandedScopeDoor::open<Fresh>(first, count));
}

}  // namespace foundation::core
