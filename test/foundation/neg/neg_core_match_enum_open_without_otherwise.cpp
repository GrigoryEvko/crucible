// Negative-compile test. This file must fail to compile.
//
// Errno is open: a number that no name gives, such as an error of a newer
// kernel, is a value too.  A match over it takes one arm otherwise for such
// a value, and the match below has none.
//
// Expected diagnostic: an open enumeration takes one arm otherwise.

#include <foundation/core/EnumMatch.h>
#include <foundation/os/Errno.h>

int main() {
    namespace core = ::foundation::core;
    using ::foundation::os::Errno;
    return core::match_enum(Errno::eintr, core::arm<Errno::eintr, Errno::eagain>([] noexcept { return 0; }));
}
