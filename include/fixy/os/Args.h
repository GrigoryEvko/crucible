#pragma once

// The argument door of fixy: the arguments of main.
//
//   args(argc, argv)   gives Args, a view of the array that the C runtime
//                      gives main
//   Args::size()       the count of the arguments
//   Args::get(i)       the CText of argument i, or no value at or past the
//                      count: one compare and a walk to the zero
//   for (CText a : args(argc, argv))
//                      each argument, in order
//
// Args copies nothing and allocates nothing.  The C runtime keeps the array
// and its texts for the life of the process, so an Args and each CText stay
// valid until the process ends.  A CText holds the count of its characters,
// which the door counts with a walk to the zero: the base calls no strlen
// of the C library.
//
// The constructor of Args checks the two parameters of main, and args is
// its spelling at the call in main.  The door takes no context: it reads no
// state of the kernel and changes nothing.
//
// This header includes no system header.

#include <foundation/core/Choice.h>
#include <foundation/core/Fatal.h>
#include <foundation/core/Region.h>
#include <foundation/core/Text.h>

#include <cstddef>

namespace fixy::os {

namespace detail {

// The CText of the text at first, which a zero ends.  The kernel gives each
// argument of the process with its zero, so the foreign view holds the zero,
// and mint_c_text finds it.  Complexity: O(n) in the characters.
[[nodiscard]] inline ::foundation::core::CText c_text_of_argument_(char const* first) noexcept {
    std::size_t count = 0;
    while (first[count] != '\0') {
        ++count;
    }
    return ::foundation::core::mint_c_text(::foundation::core::mint_foreign_text(first, count + 1).characters())
        .expect("an argument of the process ends with its zero");
}

}  // namespace detail

class ArgsCursor;

// The arguments of main: a view of argv with its count.
class Args {
public:
    // A view of the two parameters of main.  The C runtime gives a count of
    // zero or more, and a text that a zero ends for each position below the
    // count.  A count below zero, a null argv, or a null text below the
    // count is no array of the runtime, and it ends the process.
    // Complexity: O(n) in the count.
    Args(int argc, char const* const* argv) noexcept : first_{argv}, count_{count_of_(argc, argv)} {}

    [[nodiscard]] std::size_t size() const noexcept { return count_; }

    // The CText of the argument at position, or no value at or past the
    // count.
    [[nodiscard]] ::foundation::core::Option<::foundation::core::CText> get(std::size_t position) const noexcept {
        if (position >= count_) {
            return ::foundation::core::none;
        }
        return ::foundation::core::Option<::foundation::core::CText>::some(
            detail::c_text_of_argument_(first_[position]));
    }

    [[nodiscard]] ArgsCursor begin() const noexcept;
    [[nodiscard]] ArgsCursor end() const noexcept;

private:
    [[nodiscard]] static std::size_t count_of_(int argc, char const* const* argv) noexcept {
        if (argc < 0 || argv == nullptr) [[unlikely]] {
            ::foundation::core::fatal("fixy::os::Args got a negative count or a null argv, which main does not get");
        }
        auto const count = static_cast<std::size_t>(argc);
        for (std::size_t position = 0; position < count; ++position) {
            if (argv[position] == nullptr) [[unlikely]] {
                ::foundation::core::fatal("fixy::os::Args got a null text below the count, which main does not get");
            }
        }
        return count;
    }

    char const* const* first_ = nullptr;
    std::size_t count_ = 0;
};

// The position in a loop over the arguments.  The dereference gives the
// CText of the argument at the position, and a position at or past the
// count ends the process.
class ArgsCursor {
public:
    ArgsCursor(Args const& arguments, std::size_t position) noexcept : arguments_{arguments}, position_{position} {}

    [[nodiscard]] ::foundation::core::CText operator*() const noexcept {
        return arguments_.get(position_).expect("a loop reads an argument at or past the count of its Args");
    }

    ArgsCursor& operator++() noexcept {
        ++position_;
        return *this;
    }

    // Two cursors of one loop are equal at the same position.
    [[nodiscard]] bool operator==(ArgsCursor const& other) const noexcept { return position_ == other.position_; }

private:
    Args arguments_;
    std::size_t position_ = 0;
};

inline ArgsCursor Args::begin() const noexcept { return ArgsCursor{*this, 0}; }
inline ArgsCursor Args::end() const noexcept { return ArgsCursor{*this, count_}; }

// The arguments of main, from the two parameters of main: the constructor of
// Args, with the name of the door from main.
[[nodiscard]] inline Args args(int argc, char const* const* argv) noexcept { return Args{argc, argv}; }

}  // namespace fixy::os
