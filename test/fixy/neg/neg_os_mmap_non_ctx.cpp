// The first argument of the mint is the execution context, whose row is
// what admits the mapping.  The capability token a context is built from
// is not a context: it carries no row, so there is nothing to weigh the
// atoms against.  The mint refuses it at the context clause.
//
// The descriptor is -1, and nothing here is ever run.

#include <fixy/os/Mmap.h>

namespace eff = foundation::effects;
namespace perm = foundation::permissions;

namespace {
struct FileRegion final {
    using permission_row = eff::Row<>;
};
using ReadOnly = fixy::atom::mmap::with_prot<fixy::mmap::prot::ReadOnly>;
using Shared = fixy::atom::mmap::with_share<fixy::mmap::share::Shared>;
}  // namespace

int main() {
    auto const token = eff::testing::test();
    auto const owner = perm::mint_permission_root<FileRegion>();
    [[maybe_unused]] auto refused = fixy::mmap::mint_mmap<ReadOnly, Shared>(token, owner, -1, 4096);
    return 0;
}
