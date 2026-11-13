// The brand of a sole writer is the identity of one writer.  DefaultBrand
// would give each writer of a role one type, so the writer of one cell
// could write each other cell of the role.  No writer of DefaultBrand
// exists, so the probe asks the size of the cell, and that instantiates the
// class with its static_assert.

#include <foundation/Brand.h>
#include <foundation/core/Atomic.h>

#include <cstdint>

namespace {
struct HeadRole {};
}  // namespace

int main() {
    using DefaultCell =
        ::foundation::core::Atomic<std::uint64_t,
                                   ::foundation::core::SoleWriter<HeadRole, ::foundation::brand::DefaultBrand>>;
    return static_cast<int>(sizeof(DefaultCell));
}
