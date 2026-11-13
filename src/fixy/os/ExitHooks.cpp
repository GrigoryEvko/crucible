// The door of fixy to the process state that the C library keeps: the exit
// hooks (fixy/os/ExitHooks.h) and the environment (fixy/os/Environment.h).
// It is the one includer of <stdlib.h>.

#include <fixy/os/Environment.h>
#include <fixy/os/ExitHooks.h>

#include <stdlib.h>

#include <cstddef>

namespace fixy::exit::detail {

::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno> run_at_exit_(ExitHook hook) noexcept {
    if (::atexit(hook) != 0) return ::foundation::core::err(::foundation::os::Errno::enomem);
    if (::at_quick_exit(hook) != 0) return ::foundation::core::err(::foundation::os::Errno::enomem);
    return ::foundation::core::Unit{};
}

}  // namespace fixy::exit::detail

namespace fixy::env::detail {

namespace {

// The largest count of the characters of a TextView.  The count loop below
// stops there too, and the second exit keeps the optimizer from a call of
// strlen of the C library, which the rule of the symbols of the base refuses.
constexpr std::size_t kLongestValue = static_cast<std::size_t>(__PTRDIFF_MAX__);

}  // namespace

// Complexity: the scan of the C library over each variable, and one more
// pass over the characters of the value to count them.
::foundation::core::Option<::foundation::core::TextView> read_variable_(::foundation::core::CText name) noexcept {
    namespace core = ::foundation::core;
    char const* const value = ::getenv(core::detail::c_text_first_(name));
    if (value == nullptr) return core::none;
    std::size_t count = 0;
    while (count < kLongestValue && value[count] != '\0') {
        ++count;
    }
    return core::Option<core::TextView>::some(core::detail::text_over_(value, count));
}

}  // namespace fixy::env::detail
