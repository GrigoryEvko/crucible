// Negative-compile test.  This file must fail to compile.
//
// The foreign door takes the token of the region by move, so the caller
// keeps no name of the token while the region holds it.  A token that the
// caller keeps by name would open a second region over the same run.
//
// Expected diagnostic: mint_foreign_owned_region has no candidate for an
// lvalue Permission.

#include <fixy/OwnedRegion.h>
#include <foundation/Quarantine.h>
#include <foundation/permissions/Permission.h>

#include <cstddef>
#include <cstdint>

namespace {

struct ForeignRun {
    using permission_row = ::foundation::effects::Row<>;
};

}  // namespace

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the fixture stands for a buffer that a foreign caller gives")
int main() {
    std::uint64_t foreign_buffer[4]{};
    auto token = ::foundation::permissions::mint_permission_root<ForeignRun>();
    auto region = ::fixy::mint_foreign_owned_region(foreign_buffer, std::size_t{4}, token);
    static_cast<void>(region);
    return 0;
}
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
