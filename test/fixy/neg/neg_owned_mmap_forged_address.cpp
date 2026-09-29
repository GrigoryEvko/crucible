// OwnedMmap's destructor calls ::munmap on whatever address it holds,
// and is_mapped() rejects only MAP_FAILED and nullptr.  A public
// constructor over an address would therefore do two things at once: it
// would let a caller claim a mapping it never made, and it would let the
// caller name any address in the process for unmapping on scope exit.  A
// static array, a stack frame, or the middle of somebody else's mapping
// would all read as a region.
//
// This fixture is the standing witness on the direct route.  The
// constructor is private and the static factory below it performs the
// ::mmap itself, so a region exists only over an address the kernel
// returned.  Its sibling, neg_owned_mmap_forged_through_mint_linear.cpp,
// covers the route that goes through the generic Linear forwarder.

#include <fixy/OwnedMmap.h>

namespace {
struct StackRegionTag final {};
alignas(4096) char not_a_mapping[8192];
}  // namespace

int main() {
    fixy::OwnedMmap<StackRegionTag, fixy::mmap::prot::ReadOnly, fixy::mmap::share::Private> forged{
        not_a_mapping, sizeof(not_a_mapping)};
    return forged.is_mapped() ? 0 : 1;
}
