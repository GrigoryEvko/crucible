// The first argument of the mint is the execution context, whose row is
// what admits the mapping.  The capability token a context is built from
// is not a context: it carries no row, so there is nothing to weigh the
// atoms against.  The mint refuses it at the context clause.
//
// Nothing here is ever run.

#include <fixy/os/Mmap.h>

namespace eff = foundation::effects;
namespace perm = foundation::permissions;

namespace {
struct AnonRegion final {
    using permission_row = eff::Row<>;
};
using WriteAnon = fixy::atom::mmap::with_prot<fixy::mmap::prot::WriteCopy>;
using Anonymous = fixy::atom::mmap::with_share<fixy::mmap::share::Anonymous>;
}  // namespace

int main() {
    auto const token = eff::testing::test();
    auto const owner = perm::mint_permission_root<AnonRegion>();
    [[maybe_unused]] auto refused = fixy::mmap::mint_mmap_anon<WriteAnon, Anonymous>(token, owner, 4096);
    return 0;
}
