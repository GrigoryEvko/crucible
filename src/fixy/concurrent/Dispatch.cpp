// The cold refusal of the dispatch door of fixy/concurrent/Dispatch.h.

#include <fixy/concurrent/Dispatch.h>

#include <foundation/core/Fatal.h>

namespace fixy::dispatch::detail {

void refuse_nested_dispatch_() noexcept {
    ::foundation::core::fatal(
        "fixy::dispatch: a dispatch opened inside a dispatch window of the same thread, so two windows would hold two "
        "keys of one brand at the same time");
}

}  // namespace fixy::dispatch::detail
