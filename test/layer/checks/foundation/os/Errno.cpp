// The compile-time checks of foundation/os/Errno.h.

#include <foundation/os/Errno.h>

#include <foundation/core/Scalar.h>

#include <cstdint>
#include <utility>

namespace foundation::os {

namespace detail::errno_checks {

// True when no name of Errno gives a number at or above first.  Linear in
// the names.
[[nodiscard]] consteval bool names_no_number_from(std::uint16_t first) {
    for (std::size_t index = 0; core::enum_at<Errno>(index).is_some(); ++index) {
        if (std::to_underlying(core::enum_at<Errno>(index).value_or(Errno::eperm)) >= first) return false;
    }
    return true;
}

}  // namespace detail::errno_checks

// Errno is open: a number that no name gives, such as an error of a newer
// kernel, is a value too.  No name gives 4000, so decode and enum_from give
// the number 4000 itself, and bit_cast makes it with no check.
static_assert(core::OpenEnumeration<Errno> && core::BitValid<Errno>);
static_assert(detail::errno_checks::names_no_number_from(4000) && !detail::errno_checks::names_no_number_from(22));
static_assert(std::to_underlying(core::enum_from<Errno>(4000)) == 4000 && core::enum_from<Errno>(22) == Errno::einval);
static_assert(std::to_underlying(core::decode<Errno>(std::uint16_t{4000}).value_or(Errno::eperm)) == 4000);
static_assert(std::to_underlying(core::bit_cast<Errno>(std::uint16_t{4000})) == 4000);

// A word from -kLargestErrno to -1 is an error, and each other word is a value.
static_assert(result_of(0).is_ok());
static_assert(result_of(-1).is_err());
static_assert(result_of(-kLargestErrno).is_err());
static_assert(result_of(-kLargestErrno - 1).is_ok());

// unit_of keeps the error of a word and drops its value.
static_assert(unit_of(0L).is_ok());
static_assert(unit_of(7L).is_ok());
static_assert(unit_of(-static_cast<long>(Errno::eperm)).is_err());

// error_number gives the number of the kernel, and error_number_of gives 0
// for a value.
static_assert(error_number(Errno::einval) == 22);
static_assert(error_number_of(result_of(5)) == 0);
static_assert(error_number_of(result_of(-static_cast<long>(Errno::enomem))) == 12);
static_assert(error_number_of(unit_of(-static_cast<long>(Errno::eio))) == 5);

}  // namespace foundation::os
