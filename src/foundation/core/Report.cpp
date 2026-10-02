// SPDX-License-Identifier: Apache-2.0

// The cold formatter of the Report family, and the report to a sink.
//
// format_window_ writes one window of the formatted text: the bytes from
// skip to skip + capacity.  So a caller with a small buffer gets each byte
// of a long text, one window after the other, and no byte is lost.  The
// formatter uses no locale, no allocation and no global state.  It reads
// the text up to its terminating zero, which the consteval constructor of
// Fmt checked.
//
// The formatter is total for each input that a caller of the base can
// give.  A placeholder with no argument writes nothing, and a stray brace
// is written as itself.  The constructor of Fmt refuses the two at compile
// time, so they do not occur.

#include <foundation/core/Report.h>

#include <cstddef>
#include <cstdint>

namespace foundation::core::detail {

namespace {

// The capacity of the stack buffer of report_.  A text of at most this
// many bytes is written with one system call.
constexpr std::size_t report_window_bytes = 512;

// Collects the bytes of one window of the formatted text.  position_
// counts each byte of the text, also a byte outside the window.
class Window {
public:
    Window(char* out, std::size_t capacity, std::size_t skip) noexcept : out_{out}, capacity_{capacity}, skip_{skip} {}

    void put(char byte) noexcept {
        if (position_ >= skip_ && position_ - skip_ < capacity_) out_[position_ - skip_] = byte;
        ++position_;
    }

    void put(char const* bytes, std::size_t count) noexcept {
        for (std::size_t index = 0; index < count; ++index)
            put(bytes[index]);
    }

    [[nodiscard]] std::size_t position() const noexcept { return position_; }

private:
    char* out_;
    std::size_t capacity_;
    std::size_t skip_;
    std::size_t position_ = 0;
};

// Writes value in decimal.  A std::uint64_t has at most 20 digits.
void put_unsigned(Window& window, std::uint64_t value) noexcept {
    char digits[20] = {};
    std::size_t count = 0;
    do {
        digits[count] = static_cast<char>('0' + value % 10U);
        ++count;
        value /= 10U;
    } while (value != 0U);
    while (count > 0) {
        --count;
        window.put(digits[count]);
    }
}

// Writes the std::int64_t whose bits are bits.  The magnitude is the
// unsigned negation of the bits, which is exact also for the smallest
// value.
void put_signed(Window& window, std::uint64_t bits) noexcept {
    if ((bits >> 63U) != 0U) {
        window.put('-');
        put_unsigned(window, 0U - bits);
    } else {
        put_unsigned(window, bits);
    }
}

void put_argument(Window& window, FmtArg const& argument) noexcept {
    switch (argument.kind) {
        case FmtArgKind::signed_integer:
            put_signed(window, argument.bits);
            return;
        case FmtArgKind::unsigned_integer:
            put_unsigned(window, argument.bits);
            return;
        case FmtArgKind::boolean:
            if (argument.bits != 0U) {
                window.put("true", 4);
            } else {
                window.put("false", 5);
            }
            return;
        case FmtArgKind::text:
            window.put(argument.first, argument.size);
            return;
        default:
            // A kind outside the enumerators writes nothing.  The inline
            // functions of the family build each argument, so no such kind
            // occurs, and a report must not end the process.
            return;
    }
}

}  // namespace

std::size_t format_window_(char* out, std::size_t capacity, std::size_t skip, char const* text, FmtArg const* arguments,
                           std::size_t count) noexcept {
    Window window{out, capacity, skip};
    std::size_t next_argument = 0;
    // A byte that is not the terminating zero has a next byte, at worst the
    // terminating zero, so the read of cursor[1] stays in the text.
    for (char const* cursor = text; *cursor != '\0'; ++cursor) {
        char const byte = *cursor;
        if (byte == '{' && cursor[1] == '}') {
            if (next_argument < count) put_argument(window, arguments[next_argument]);
            ++next_argument;
            ++cursor;
        } else if ((byte == '{' || byte == '}') && cursor[1] == byte) {
            window.put(byte);
            ++cursor;
        } else {
            window.put(byte);
        }
    }
    return window.position();
}

void report_(Sink sink, char const* text, FmtArg const* arguments, std::size_t count) noexcept {
    char window[report_window_bytes] = {};
    std::size_t skip = 0;
    std::size_t length = 0;
    do {
        length = format_window_(window, report_window_bytes, skip, text, arguments, count);
        std::size_t const rest = length - skip;
        std::size_t const taken = rest < report_window_bytes ? rest : report_window_bytes;
        write_sink_(sink, window, taken);
        skip += taken;
    } while (skip < length);
}

}  // namespace foundation::core::detail
