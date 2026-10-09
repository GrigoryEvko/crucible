// The CText of a temporary PathText is refused.  The CText borrows the buffer
// of the path, and the buffer of a temporary ends with the full expression,
// so a door that read the CText after it would read freed stack memory.

#include <foundation/os/PathText.h>

namespace {

[[maybe_unused]] ::foundation::core::CText dangling() noexcept { return ::foundation::os::PathText<8>{}.c_text(); }

}  // namespace
