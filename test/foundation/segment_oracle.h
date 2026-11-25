#pragma once

// The elements and the oracle of the tests of a SegmentList and of a LiveList.
// The oracle holds the values that the reads of a list or of a prefix must
// give, and require_agreement compares each read with it.

#include <fixy/check/Check.h>
#include <foundation/core/Array.h>
#include <foundation/core/Index.h>
#include <foundation/core/Region.h>
#include <foundation/core/SegmentList.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace crucible::test {

// Elements of four sizes: segment 0 has 256, 32, 4 and 1 slots.  A large
// element holds its value in its first word and the complement in its last
// one, and a byte holds the low bits of its value.
template <std::size_t Between>
struct Wide final {
    std::uint64_t value = 0;
    ::foundation::core::FixedArray<std::uint64_t, Between> between{};
    std::uint64_t complement = ~std::uint64_t{0};
};
using Event = Wide<7>;
using Page = Wide<38>;
static_assert(sizeof(Event) == 72 && sizeof(Page) == 320);

template <class T>
[[nodiscard]] T element_of(std::uint64_t value) noexcept {
    if constexpr (std::is_class_v<T>) {
        return T{.value = value, .complement = ~value};
    } else {
        return static_cast<T>(value);
    }
}

template <class T>
[[nodiscard]] bool holds(T const& element, std::uint64_t value) noexcept {
    if constexpr (std::is_class_v<T>) {
        return element.value == value && element.complement == ~value;
    } else {
        return element == static_cast<T>(value);
    }
}

inline constexpr std::size_t oracle_slots = 1024;

// The values that a list must hold: a FixedArray and a count.
struct Oracle final {
    ::foundation::core::FixedArray<std::uint64_t, oracle_slots> values{};
    std::size_t count = 0;

    void append(std::uint64_t value) noexcept {
        values.set(::foundation::core::Index<oracle_slots>::check(count).expect("the oracle has room"), value);
        ++count;
    }

    [[nodiscard]] std::uint64_t at(std::size_t position) const noexcept {
        CRUCIBLE_FATAL_INVARIANT(position < count);
        return *values.get(position).expect("the position is below the count");
    }
};

// Each read of a list or of a prefix gives the first elements of the oracle in
// order, and no element past them: get, the loop, the walk with each kind of
// terminal, and the walk of the segments.  Complexity: O(count).
template <class Test, class Reads>
void require_first_elements(Test const& test, Reads const& reads, Oracle const& oracle, std::size_t count) {
    using T = std::remove_const_t<typename Reads::element_type>;
    CRUCIBLE_TEST_ASSERT(test, reads.size() == count);
    for (std::size_t position = 0; position != count; ++position) {
        T const& element = *reads.get(position).expect("the position is below the count");
        CRUCIBLE_TEST_ASSERT(test, holds(element, oracle.at(position)));
    }
    for (std::size_t past = 0; past != 3; ++past) {
        CRUCIBLE_TEST_ASSERT(test, reads.get(count + past).is_none());
        CRUCIBLE_TEST_ASSERT(test, reads.get(~past).is_none());
    }
    std::size_t position = 0;
    for (T const& element : reads) {
        CRUCIBLE_TEST_ASSERT(test, holds(element, oracle.at(position)));
        ++position;
    }
    CRUCIBLE_TEST_ASSERT(test, position == count);
    std::size_t const folded =
        reads.walk().fold(std::size_t{0}, [&oracle, count](std::size_t at, T const& element) noexcept {
            return at < count && holds(element, oracle.at(at)) ? at + 1 : count + 1;
        });
    CRUCIBLE_TEST_ASSERT(test, folded == count && reads.walk().size() == count);
    // Each run starts where the run before it ended, and no run is empty.
    std::size_t walked = 0;
    for (::foundation::core::View<T const> run : reads.segments()) {
        CRUCIBLE_TEST_ASSERT(test, run.size() != 0);
        for (T const& element : run) {
            CRUCIBLE_TEST_ASSERT(test, holds(element, oracle.at(walked)));
            ++walked;
        }
    }
    CRUCIBLE_TEST_ASSERT(test, walked == count);
    if (count == 0) {
        CRUCIBLE_TEST_ASSERT(test, reads.last().is_none());
    } else {
        T const& last = *reads.last().expect("the reads give an element");
        CRUCIBLE_TEST_ASSERT(test, holds(last, oracle.at(count - 1)));
    }
}

// Each read gives all the elements of the oracle.
template <class Test, class Reads>
void require_agreement(Test const& test, Reads const& reads, Oracle const& oracle) {
    require_first_elements(test, reads, oracle, oracle.count);
}

// True when a count is one before, at, or one after the end of a segment, for
// segment 0 of first slots.
[[nodiscard]] inline bool is_near_segment_edge(std::size_t count, std::size_t first) noexcept {
    std::size_t end = 0;
    for (std::size_t slots = first; end <= count + 1; slots *= 2) {
        if (count + 1 == end || count == end || count == end + 1) return true;
        end += slots;
    }
    return count + 1 == end || count == end || count == end + 1;
}

}  // namespace crucible::test
