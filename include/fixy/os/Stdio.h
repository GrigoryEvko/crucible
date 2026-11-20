#pragma once

// The doors of the standard streams of the process.
//
//   LineReader<Capacity>   reads standard input one line at a time, into a
//                          buffer of its own that holds a line of at most
//                          Capacity characters
//
// THE LINES
//   next_line(ctx) gives the next line of standard input as a TextView,
//   without its newline.  The view borrows the buffer of the reader, and
//   the next call writes that buffer, so a caller copies a line that it
//   keeps.  The end of the input gives no value, and an empty line gives an
//   empty view, so the two differ.  A last line with no newline is a line.
//
//   A line of more than Capacity characters gives the error LineTooLong,
//   and no part of the line.  The reader then drops the rest of that line
//   up to its newline, so the call after the error gives the line after it.
//   An error of the kernel gives its Errno.  A read that a signal
//   interrupts asks again, so eintr never comes out.  The reader keeps no
//   state outside its object, and each object reads with its own buffer.
//   Two readers of standard input in one process would share its bytes in
//   no order, so a program holds one.
//
//   The reads go through the door file src/fixy/os/Stdio.cpp, which issues
//   the raw read of foundation/os/Syscall.h on descriptor 0.  A read can
//   park the caller, so the call takes a context that owns IO and Block.
//   next_line_with_ takes a read step in the place of the door, as the full
//   transfers of fixy/os/Fs.h do, so a test gives the reader a step of its
//   own.  Complexity: one compare for each byte of the input, one copy of
//   the bytes after the last line when the buffer fills, and one read for
//   each part of the input that the kernel gives.

#include <fixy/os/Fd.h>
#include <foundation/core/Choice.h>
#include <foundation/core/OneOf.h>
#include <foundation/core/Region.h>
#include <foundation/core/Text.h>
#include <foundation/effects/Ctx.h>
#include <foundation/os/Errno.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace fixy::os {

// The error of a line that holds more characters than the buffer of its
// reader.  The reader gives no part of such a line.
struct LineTooLong final {};

// The error of a read of a line: the error of the kernel, or a line that
// does not fit.
using LineError = ::foundation::core::OneOf<::foundation::os::Errno, LineTooLong>;

namespace detail {

// One read of standard input into the count bytes at first.  It gives the
// count of the bytes that the kernel gave, zero at the end of the input, or
// the error of the kernel.  A read that a signal interrupts runs again.
[[nodiscard]] ::foundation::core::Result<std::size_t, ::foundation::os::Errno>
read_standard_input_(char* first, std::size_t count) noexcept;

}  // namespace detail

// A reader of the lines of standard input (THE LINES).  It does not copy
// and does not move, because a line that it gave borrows its buffer.
template <std::size_t Capacity>
    requires(Capacity > 0 && Capacity < static_cast<std::size_t>(__PTRDIFF_MAX__))
class LineReader final {
    // The state of the input after the last read.
    enum class Input : std::uint8_t {
        open,
        ended
    };

    // One byte more than the longest line, for its newline.
    static constexpr std::size_t buffer_bytes = Capacity + 1;

    char bytes_[buffer_bytes]{};
    std::size_t first_ = 0;
    std::size_t last_ = 0;
    Input input_ = Input::open;
    bool is_dropping_ = false;

    using LineResult = ::foundation::core::Result<::foundation::core::Option<::foundation::core::TextView>, LineError>;

    // The line from first_ to end.  A newline at end is no part of the line,
    // and the unread bytes then start after it.
    [[nodiscard]] LineResult take_line_(std::size_t end) noexcept {
        ::foundation::core::TextView const line = ::foundation::core::detail::text_over_(bytes_ + first_, end - first_);
        first_ = end < last_ ? end + 1 : end;
        return LineResult{::foundation::core::Option<::foundation::core::TextView>::some(line)};
    }

    // Moves the bytes after the last line to the start of the buffer.
    void compact_() noexcept {
        std::size_t const count = last_ - first_;
        for (std::size_t index = 0; index < count; ++index)
            bytes_[index] = bytes_[first_ + index];
        first_ = 0;
        last_ = count;
    }

public:
    LineReader() noexcept = default;
    LineReader(LineReader const&) = delete("a line that the reader gave borrows its buffer");
    LineReader& operator=(LineReader const&) = delete("a line that the reader gave borrows its buffer");
    LineReader(LineReader&&) = delete("a line that the reader gave borrows its buffer");
    LineReader& operator=(LineReader&&) = delete("a line that the reader gave borrows its buffer");
    ~LineReader() = default;

    // The next line of standard input (THE LINES).
    template <::foundation::effects::IsExecCtx Ctx>
        requires ::fixy::fs::CtxAdmitsFs<Ctx>
    [[nodiscard]] LineResult next_line(Ctx const&) & noexcept {
        return next_line_with_([](::foundation::core::View<char> room) noexcept {
            return detail::read_standard_input_(first_of_(::foundation::core::detail::ViewDoor::key(), room),
                                                room.size());
        });
    }

    // The next line, with step in the place of the read of standard input:
    // step(room) puts bytes at the start of the View room, and gives their
    // count, zero at the end of the input, or an Errno.
    //
    // Each pass searches the unread bytes for a newline.  A newline ends a
    // line, or the dropped rest of a long line.  With no newline, the end of
    // the input ends the last line, a full buffer is a long line, and each
    // other case reads more bytes after the unread ones.
    template <class Step>
    [[nodiscard]] LineResult next_line_with_(Step step) & noexcept {
        while (true) {
            std::size_t position = first_;
            while (position < last_ && bytes_[position] != '\n')
                ++position;
            if (position < last_) {
                if (!is_dropping_) return take_line_(position);
                is_dropping_ = false;
                first_ = position + 1;
                continue;
            }
            if (input_ == Input::ended) {
                bool const has_last_line = !is_dropping_ && first_ < last_;
                bool const is_too_long = last_ - first_ > Capacity;
                is_dropping_ = false;
                if (has_last_line && !is_too_long) return take_line_(last_);
                first_ = last_;
                if (has_last_line) return LineResult{::foundation::core::err(LineError{LineTooLong{}})};
                return LineResult{::foundation::core::Option<::foundation::core::TextView>{::foundation::core::none}};
            }
            compact_();
            if (last_ == buffer_bytes) {
                last_ = 0;
                if (!is_dropping_) {
                    is_dropping_ = true;
                    return LineResult{::foundation::core::err(LineError{LineTooLong{}})};
                }
            }
            ::foundation::core::Result<std::size_t, ::foundation::os::Errno> read =
                step(::foundation::core::detail::view_over_<char>(bytes_ + last_, buffer_bytes - last_));
            for (::foundation::os::Errno const error : read.err()) {
                return LineResult{::foundation::core::err(LineError{error})};
            }
            std::size_t const count = std::move(read).expect("a read with no error gives a count");
            if (count == 0) {
                input_ = Input::ended;
            } else {
                last_ += count <= buffer_bytes - last_ ? count : buffer_bytes - last_;
            }
        }
    }
};

}  // namespace fixy::os
