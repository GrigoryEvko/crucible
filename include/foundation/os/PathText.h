#pragma once

// A path for a call of the kernel that a program builds at run time: an
// owned buffer of at most Capacity characters and a zero after the last one.
//
// A CText from a string literal is a path that the build knows.  A path of
// sysfs or of /proc can hold a number, such as the id of a CPU, so a door
// builds it at run time.  Each append keeps the zero after the last
// character, and an append refuses a zero byte.  So c_text() gives a CText
// that a reader of the kernel reads up to the zero, and no further.
//
// An append that does not fit gives the error enametoolong, and an append of
// a zero byte gives the error einval.  On an error the path keeps the
// characters that it held before the append.
//
// The buffer is a FixedText of Capacity + 1 characters.  Its count is the
// count of the path, and the character after the count is the zero.

#include <foundation/core/Choice.h>
#include <foundation/core/Text.h>
#include <foundation/os/Errno.h>

#include <cstddef>
#include <cstdint>

namespace foundation::os {

template <std::size_t Capacity>
    requires(Capacity > 0 && Capacity < static_cast<std::size_t>(__PTRDIFF_MAX__))
class PathText {
    core::FixedText<Capacity + 1> text_{};

    // The decimal digits of the largest value of std::uint64_t.
    static constexpr std::size_t kDecimalDigits = 20;

    // Writes the count characters at first after the path, and the zero
    // after them.  The caller checked that they fit and hold no zero.
    constexpr void put_(char const* first, std::size_t count) noexcept {
        char* const bytes = core::detail::text_bytes_(text_);
        std::size_t const size = text_.size();
        for (std::size_t index = 0; index < count; ++index) {
            bytes[size + index] = first[index];
        }
        bytes[size + count] = '\0';
        core::detail::text_resize_(text_, size + count);
    }

public:
    static constexpr std::size_t capacity = Capacity;

    // The empty path.
    constexpr PathText() noexcept = default;

    // The count of the characters, without the zero.
    [[nodiscard]] constexpr std::size_t size() const noexcept {
        std::size_t const count = text_.size();
        [[assume(count <= Capacity)]];
        return count;
    }

    // Appends one character.
    [[nodiscard]] constexpr core::Result<core::Unit, Errno> append(char character) noexcept {
        if (character == '\0') return core::err(Errno::einval);
        if (size() == Capacity) return core::err(Errno::enametoolong);
        put_(&character, 1);
        return core::Unit{};
    }

    // Appends each character of text.  The cost is one compare and one copy
    // for each character.
    [[nodiscard]] constexpr core::Result<core::Unit, Errno> append(core::TextView text) noexcept {
        char const* const first = core::detail::text_first_(text);
        std::size_t const count = text.size();
        if (count > Capacity - size()) return core::err(Errno::enametoolong);
        for (std::size_t index = 0; index < count; ++index) {
            if (first[index] == '\0') return core::err(Errno::einval);
        }
        put_(first, count);
        return core::Unit{};
    }

    [[nodiscard]] constexpr core::Result<core::Unit, Errno> append(core::CText text) noexcept {
        return append(text.view());
    }

    // Appends the decimal digits of value, with no sign and no leading zero.
    [[nodiscard]] constexpr core::Result<core::Unit, Errno> append_decimal(std::uint64_t value) noexcept {
        char reversed[kDecimalDigits]{};
        std::size_t count = 0;
        do {
            reversed[count] = static_cast<char>('0' + value % 10);
            value /= 10;
            ++count;
        } while (value != 0);
        if (count > Capacity - size()) return core::err(Errno::enametoolong);
        char digits[kDecimalDigits]{};
        for (std::size_t index = 0; index < count; ++index) {
            digits[index] = reversed[count - 1 - index];
        }
        put_(digits, count);
        return core::Unit{};
    }

    // The characters, with the zero after them.  The CText borrows this
    // buffer, so a CText of a temporary path is refused: it would dangle at
    // the end of the full expression.
    [[nodiscard]] constexpr core::CText c_text() const& noexcept {
        return core::detail::c_text_over_(core::detail::text_first_(text_.view()), size());
    }
    core::CText c_text() const&& = delete("a CText of a temporary PathText points into an object that the full "
                                          "expression ends");
};

}  // namespace foundation::os
