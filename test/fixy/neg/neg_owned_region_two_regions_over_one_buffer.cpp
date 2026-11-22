// Negative-compile test.  This file must fail to compile.
//
// Two regions of the tags TagA and TagB over one buffer of four words would
// each go to a parallel door on a different thread, and the two runs would
// race on each word.  No door builds a region over a raw address: the scope
// doors hold their storage for their frame, the arena door takes fresh
// storage, and the foreign door stands only at the edge of a foreign ABI.
//
// Expected diagnostic: no mint_owned_region takes a pointer and a count.

#include <fixy/OwnedRegion.h>
#include <foundation/permissions/Permission.h>

#include <cstddef>
#include <cstdint>

namespace {

struct TagA {
    using permission_row = ::foundation::effects::Row<>;
};
struct TagB {
    using permission_row = ::foundation::effects::Row<>;
};

}  // namespace

int main() {
    std::uint64_t storage[4]{};
    auto first =
        ::fixy::mint_owned_region(storage, std::size_t{4}, ::foundation::permissions::mint_permission_root<TagA>());
    static_cast<void>(first);
    return 0;
}
