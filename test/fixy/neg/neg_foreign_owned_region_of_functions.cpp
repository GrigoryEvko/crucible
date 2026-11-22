// Negative-compile test.  This file must fail to compile.
//
// The foreign door builds a region over a run of objects that a foreign ABI
// gives.  A pointer to a function names no run of objects, so a region over
// it would hand out views of code.  The door takes only an object type.
//
// Expected diagnostic: mint_foreign_owned_region has no candidate for a
// function type.

#include <fixy/OwnedRegion.h>
#include <foundation/Quarantine.h>
#include <foundation/permissions/Permission.h>

#include <cstddef>

namespace {

struct ForeignRun {
    using permission_row = ::foundation::effects::Row<>;
};

void foreign_entry() noexcept {}

}  // namespace

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the fixture gives the foreign door the address of a foreign entry")
int main() {
    auto region = ::fixy::mint_foreign_owned_region(&foreign_entry, std::size_t{1},
                                                    ::foundation::permissions::mint_permission_root<ForeignRun>());
    static_cast<void>(region);
    return 0;
}
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
