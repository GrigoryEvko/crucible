// A region carries the brand of the permission it was built from, so
// two regions of one tag minted from two roots are two types, and a
// callee that asks for two views of one region refuses the pair by
// deduction.

#include <fixy/OwnedRegion.h>
#include <foundation/permissions/Permission.h>

#include <cstddef>

namespace {
struct Buffer {
    using permission_row = ::foundation::effects::Row<>;
};

template <class Brand>
constexpr void same_region(::fixy::OwnedRegion<int, Buffer, Brand>&, ::fixy::OwnedRegion<int, Buffer, Brand>&) noexcept {}
}  // namespace

int main() {
    int storage_a[4] = {};
    int storage_b[4] = {};
    auto first = ::fixy::mint_owned_region(storage_a, std::size_t{4},
                                           ::foundation::permissions::mint_permission_root<Buffer>());
    auto second = ::fixy::mint_owned_region(storage_b, std::size_t{4},
                                            ::foundation::permissions::mint_permission_root<Buffer>());
    same_region(first, second);
    return 0;
}
