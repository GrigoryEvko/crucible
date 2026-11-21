#pragma once

// A list of reflections in static storage, for an expansion statement.
//
//   reflection_list(range)      the reflections of a range, as one array of
//                               static storage
//   reflection_list<Owner>(range)
//                               the same list, with a call that depends on
//                               the reflection Owner
//   list.size()                 the count of the reflections
//   list.is_empty()             true when the list holds no reflection
//   list[index]                 the reflection at index, below the count
//   template for (constexpr std::meta::info item : list)
//                               walks each reflection in the order of the range
//
// std::define_static_array gives the same array as a std::span.  A list holds
// the first reflection and the count, and its cursor holds one place, so the
// caller names no std::span and no pointer.  Each member is consteval: a
// reflection exists only in a constant evaluation.  A read at or past the
// count is no constant expression, so the build stops at it.
//
// The first instantiation of std::define_static_array in a unit costs about
// 68 M instructions (foundation/reflect/EnumName.h).  So a walk that needs
// no list, such as the walks of EnumName.h, keeps an array of its own size.

#include <foundation/reflect/Anchor.h>

#include <cstddef>
#include <meta>

namespace foundation::reflect {

namespace detail {

// A read of a list at or past its count calls this function.  It has no
// definition, so the call is no constant expression, and the error of the
// compiler names the rule.
void reflection_list_read_past_its_count() noexcept;

}  // namespace detail

class ReflectionList;

// One place in a ReflectionList.  An expansion statement reads the
// reflection at a place, moves a place by a step, and measures the distance
// of two places.
class ReflectionCursor final {
    std::meta::info const* at_ = nullptr;

    friend class ReflectionList;

    consteval explicit ReflectionCursor(std::meta::info const* at) noexcept : at_{at} {}

public:
    // User-provided, so the cursor is not trivially copyable, and no
    // std::bit_cast builds a cursor from bytes.  With no trivial constructor
    // it is not an implicit-lifetime type either.
    consteval ReflectionCursor(ReflectionCursor const& other) noexcept : at_{other.at_} {}
    consteval ReflectionCursor& operator=(ReflectionCursor const& other) noexcept {
        at_ = other.at_;
        return *this;
    }

    [[nodiscard]] consteval std::meta::info operator*() const noexcept { return *at_; }

    [[nodiscard]] consteval ReflectionCursor operator+(std::ptrdiff_t step) const noexcept {
        return ReflectionCursor{at_ + step};
    }

    [[nodiscard]] consteval std::ptrdiff_t operator-(ReflectionCursor other) const noexcept { return at_ - other.at_; }

    consteval ReflectionCursor& operator++() noexcept {
        ++at_;
        return *this;
    }

    [[nodiscard]] friend consteval bool operator==(ReflectionCursor left, ReflectionCursor right) noexcept {
        return left.at_ == right.at_;
    }
};

// The reflections of one range in static storage.
class ReflectionList final {
    std::meta::info const* first_ = nullptr;
    std::size_t size_ = 0;

    template <class Range>
    friend consteval ReflectionList reflection_list(Range const& reflections);

    consteval ReflectionList(std::meta::info const* first, std::size_t size) noexcept : first_{first}, size_{size} {}

public:
    // User-provided, so the list is not trivially copyable, and no
    // std::bit_cast builds a list from bytes.  With no trivial constructor
    // it is not an implicit-lifetime type either.
    consteval ReflectionList(ReflectionList const& other) noexcept : first_{other.first_}, size_{other.size_} {}
    consteval ReflectionList& operator=(ReflectionList const& other) noexcept {
        first_ = other.first_;
        size_ = other.size_;
        return *this;
    }

    [[nodiscard]] consteval std::size_t size() const noexcept { return size_; }
    [[nodiscard]] consteval bool is_empty() const noexcept { return size_ == 0; }

    // The reflection at index.  An index at or past the count is no constant
    // expression.
    [[nodiscard]] consteval std::meta::info operator[](std::size_t index) const noexcept {
        if (index >= size_) detail::reflection_list_read_past_its_count();
        return first_[index];
    }

    [[nodiscard]] consteval ReflectionCursor begin() const noexcept { return ReflectionCursor{first_}; }
    [[nodiscard]] consteval ReflectionCursor end() const noexcept { return ReflectionCursor{first_ + size_}; }
};

// The list of the reflections of the range, in one array of static storage.
// Each call with the same reflections gives the same array.  Complexity: O(n)
// in a constant evaluation, and nothing at run time.
template <class Range>
[[nodiscard]] consteval ReflectionList reflection_list(Range const& reflections) {
    auto const array = std::define_static_array(reflections);
    return ReflectionList{array.data(), array.size()};
}

// The list of the reflections of the range, as reflection_list(range) gives
// it, with a call that depends on Owner.  Owner is the reflection of what the
// list belongs to: ^^T for a template type parameter T, or a template
// parameter of the type std::meta::info.
//
// A query of a fixed entity, such as the members of one namespace, depends on
// no template parameter.  So GCC evaluates reflection_list(query) where its
// template stands, in each unit that includes the header (Anchor.h).  The
// template argument Owner makes the call depend on Owner, so only a unit that
// instantiates the enclosing template evaluates it.  A probe unit that
// defines a template and never instantiates it measures this: with
// reflection_list<^^T>(query) it adds no instruction, and with
// reflection_list(query) it adds about 105 M.  A query that reads a template
// parameter, such as std::meta::enumerators_of(^^T), needs no Owner.
template <std::meta::info Owner, class Range>
[[nodiscard]] consteval ReflectionList reflection_list(Range const& reflections) {
    return ::foundation::reflect::reflection_list(static_cast<anchored_t<Owner, Range> const&>(reflections));
}

}  // namespace foundation::reflect
