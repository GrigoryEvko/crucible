// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Cipher::record_event refuses a context whose row holds Alloc alone.  An
// allocating caller holds neither IO nor Block, and allocation is on an
// axis of its own, so it does not come near the gate.

#include <crucible/Cipher.h>
#include <fixy/Ctx.h>

namespace eff = ::foundation::effects;

int main() {
    const ::fixy::TestRunnerCtx store_ctx{eff::testing::test()};
    auto cipher =
        ::crucible::Cipher::open(store_ctx, ::fixy::mint_tagged<::fixy::tags::source::External>(
                                                std::filesystem::path{"/tmp/crucible_neg_record_event_alloc_only"}));
    const auto view = cipher.mint_open_view(store_ctx);
    const auto alloc_only = store_ctx.in_row<eff::Row<eff::Effect::Alloc>>();
    cipher.record_event(alloc_only, view, ::crucible::ContentHash{1u}, std::uint64_t{1u});
    return 0;
}
