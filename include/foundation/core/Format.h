#pragma once

// The formatter of the Report family into a FixedText.
//
//   format(out, fmt, args...)   writes the text with the arguments into out,
//                               from its start, and gives a Result: the
//                               TextView of the text, or Truncated when the
//                               text needs more than the capacity of out
//
// The text and the arguments obey the rules of foundation/core/Report.h,
// and the bytes are the bytes that report() writes for the same call.  On
// Truncated, out holds the first characters of the text, as many as its
// capacity, and the caller can read them through out.view().  The text and
// the view never leave the buffer of out.
//
// The view borrows out, so format() refuses a temporary FixedText.

#include <foundation/core/Choice.h>
#include <foundation/core/Report.h>
#include <foundation/core/Text.h>

#include <cstddef>
#include <type_traits>

namespace foundation::core {

// The error of a format() whose text needs more characters than the
// capacity of its FixedText.
struct Truncated final {};

template <std::size_t Capacity, class... Args>
[[nodiscard]] Result<TextView, Truncated> format(FixedText<Capacity>& out,
                                                 Fmt<std::type_identity_t<Args>...> const& fmt, Args... args) noexcept {
    std::size_t length = 0;
    if constexpr (sizeof...(Args) == 0) {
        length =
            detail::format_window_(detail::text_bytes_(out), Capacity, 0, detail::FmtDoor::text_of_(fmt), nullptr, 0);
    } else {
        detail::FmtArg const arguments[sizeof...(Args)] = {detail::fmt_arg_of_(args)...};
        length = detail::format_window_(detail::text_bytes_(out), Capacity, 0, detail::FmtDoor::text_of_(fmt),
                                        arguments, sizeof...(Args));
    }
    detail::text_resize_(out, length);
    if (length > Capacity) return err(Truncated{});
    return out.view();
}

template <std::size_t Capacity, class... Args>
void format(FixedText<Capacity>&& out, Fmt<std::type_identity_t<Args>...> const& fmt, Args... args) =
    delete("format() gives a view of its FixedText, and a temporary FixedText ends with the full expression");

}  // namespace foundation::core
