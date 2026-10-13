// The exit hook door of fixy (fixy/os/ExitHooks.h).  It is the one includer
// of <stdlib.h>.

#include <fixy/os/ExitHooks.h>

#include <stdlib.h>

namespace fixy::exit::detail {

::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno> run_at_exit_(ExitHook hook) noexcept {
    if (::atexit(hook) != 0) return ::foundation::core::err(::foundation::os::Errno::enomem);
    if (::at_quick_exit(hook) != 0) return ::foundation::core::err(::foundation::os::Errno::enomem);
    return ::foundation::core::Unit{};
}

}  // namespace fixy::exit::detail
