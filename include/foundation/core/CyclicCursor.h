#pragma once

// A cursor that goes around a run of at least one element, such as the ops of
// a region that a replay follows again and again.  It is never at the end of
// its run: a step from the last element goes back to the first, and tells
// that it did.  So a read needs no test.  Its one door takes a cursor of a run
// (foundation/core/Cursor.h) that is not at its end, and checks that one
// time.  The header stands apart from Cursor.h, so only its users read it.

#include <foundation/ByteSeal.h>
#include <foundation/core/Cursor.h>
#include <foundation/core/Fatal.h>

#include <cstddef>

namespace foundation::core {

// The result of one step of a cyclic cursor: the cursor moved to the next
// element, or it went from the last element back to the first.
enum class CycleStep : unsigned char {
    inside = 0,
    wrapped = 1,
};

template <class Element>
class CyclicCursor {
    Element* first_ = nullptr;
    Element* at_ = nullptr;
    Element* end_ = nullptr;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    // The cursor reads the element of first and the count that follows it,
    // so the door checked first before its end.
    constexpr CyclicCursor(Element* first, std::size_t count) noexcept
        : first_{first}, at_{first}, end_{first + count} {}

public:
    // The cyclic cursor at first, over the run from first to the end of its
    // run.  A cursor at the end of its run has no element to go around, so
    // it ends the process in each build.  A holder calls this one time for
    // each run, such as at the activation of a region.
    [[nodiscard]] static constexpr CyclicCursor cycle_from(ViewCursor<Element> first) noexcept {
        if (first.is_at_end()) [[unlikely]] {
            fatal("a cycle of a run with no element");
        }
        return CyclicCursor{&*first, first.remaining()};
    }

    // The element at the cursor.  The cursor is never at the end, so the
    // read has no test.
    [[nodiscard]] constexpr Element& operator*() const noexcept { return *at_; }

    // A step to the next element, or from the last element back to the
    // first.  The one compare is the test of the wrap.
    constexpr CycleStep step() noexcept {
        ++at_;
        if (at_ == end_) [[unlikely]] {
            at_ = first_;
            return CycleStep::wrapped;
        }
        return CycleStep::inside;
    }

    // The cursor at the first element again.
    constexpr void rewind() noexcept { at_ = first_; }

    // The count of the elements before the cursor in its run.
    [[nodiscard]] constexpr std::size_t position() const noexcept { return static_cast<std::size_t>(at_ - first_); }

    // The count of the elements of the run, at least one.
    [[nodiscard]] constexpr std::size_t count() const noexcept { return static_cast<std::size_t>(end_ - first_); }
};

}  // namespace foundation::core
