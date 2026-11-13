#pragma once

// The position of a loop over a run of elements: the cursor of a View of
// foundation/core/Region.h and of a TextView of foundation/core/Text.h.
//
// A cursor knows the end of its run.  A read or a step at the end ends the
// process in each build, so a cursor never leaves its run.  A loop compares
// the cursor with the end before each read and each step, and the optimizer
// removes the two checks there, so the count of the run is the only bound of
// the loop.
//
// The constructor of a cursor takes the key detail::ViewCursorKey, and only
// detail::ViewCursorDoor builds the key.  The key is no template, so no
// explicit specialization of a template reaches it, and this header names
// no friend that a different header defines.  The seal refuses std::bit_cast
// and a lifetime start over bytes.  The owners of a run in the base layer,
// View and TextView, give out cursors through the door.  The rule owners
// keeps each use of namespace detail outside the base out.

#include <foundation/ByteSeal.h>
#include <foundation/core/Fatal.h>

namespace foundation::core {

namespace detail {

struct ViewCursorDoor;

class [[nodiscard]] ViewCursorKey final {
    friend struct ViewCursorDoor;

    constexpr ViewCursorKey() noexcept {}
    constexpr ViewCursorKey(ViewCursorKey const&) noexcept {}

public:
    ViewCursorKey& operator=(ViewCursorKey const&) = delete("a key of a cursor lives for one construction");
};

struct ViewCursorDoor {
    [[nodiscard]] static constexpr ViewCursorKey key() noexcept { return ViewCursorKey{}; }
};

}  // namespace detail

template <class Element>
class ViewCursor {
    Element* at_ = nullptr;
    Element* end_ = nullptr;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

public:
    // The cursor at at of a run that ends at end.  The caller is an owner of
    // the run in the base layer, which proves that at and end bound one run.
    constexpr ViewCursor(detail::ViewCursorKey const&, Element* at, Element* end) noexcept : at_{at}, end_{end} {}

    [[nodiscard]] constexpr Element& operator*() const noexcept {
        if (at_ == end_) [[unlikely]] {
            fatal("a read through the end cursor of a View");
        }
        return *at_;
    }

    constexpr ViewCursor& operator++() noexcept {
        if (at_ == end_) [[unlikely]] {
            fatal("a step past the end cursor of a View");
        }
        ++at_;
        return *this;
    }

    [[nodiscard]] friend constexpr bool operator==(ViewCursor const& left, ViewCursor const& right) noexcept {
        return left.at_ == right.at_;
    }
};

}  // namespace foundation::core
