// The abort door of the base (foundation/os/Abort.h).  The builtin
// calls abort of the C library, and the file includes no header of the C
// library.

#include <foundation/os/Abort.h>

namespace foundation::os {

void abort_process() noexcept { __builtin_abort(); }

}  // namespace foundation::os
