// The compile-time checks of foundation/os/Errno.h.

#include <foundation/os/Errno.h>

namespace foundation::os {

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
