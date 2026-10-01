#include <foundation/permissions/Permission.h>

#include <foundation/diag/Catalog.h>

#include <cstdio>
#include <cstdlib>

namespace foundation::permissions {

// The cold path of a lend that found the share count at its maximum.  It
// is in a source file, so a translation unit that includes Permission.h
// does not read the diagnostic catalog.
void shared_permission_pool_saturated_abort_() noexcept {
    using Tag = ::foundation::diag::SharedPermissionPoolSaturated;
    std::fprintf(stderr,
                 "crucible: fatal contract violation: %.*s\n"
                 "  description: %.*s\n"
                 "  remediation: %.*s\n",
                 static_cast<int>(Tag::name.size()), Tag::name.data(), static_cast<int>(Tag::description.size()),
                 Tag::description.data(), static_cast<int>(Tag::remediation.size()), Tag::remediation.data());
    std::abort();
}

}  // namespace foundation::permissions
