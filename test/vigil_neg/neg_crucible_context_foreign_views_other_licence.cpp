// Negative-compile test.  This file must fail to compile.
//
// Only foreign_block_stays states that the block of a foreign owner lives for the
// time of the scope.  A type of the caller states nothing.

#include <crucible/CrucibleContext.h>

namespace {

struct OwnStatement final {};

}  // namespace

int main() {
    auto const test = ::foundation::effects::testing::test();
    crucible::CrucibleContext ctx{test.alloc};
    auto const cv = ctx.mint_compiled_view(::foundation::effects::testing::foreground<crucible::Vigil>());
    auto given = ctx.with_foreign_views<OwnStatement>(::fixy::HeapArray<crucible::ForeignView>{}, cv,
                                                      [](crucible::CrucibleContext&) noexcept {});
    static_cast<void>(given);
    return 0;
}
