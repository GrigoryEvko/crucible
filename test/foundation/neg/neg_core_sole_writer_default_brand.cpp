// The brand of a sole writer is the identity of one writer.  DefaultBrand
// would give each writer of a role one type, so the writer of one cell
// could write each other cell of the role.

#include <foundation/Brand.h>
#include <foundation/core/Atomic.h>

#include <cstdint>

namespace {
struct HeadRole {};
}  // namespace

int main() {
    ::foundation::core::Atomic<std::uint64_t,
                               ::foundation::core::SoleWriter<HeadRole, ::foundation::brand::DefaultBrand>>
        head{0};
    return static_cast<int>(head.load_acquire());
}
