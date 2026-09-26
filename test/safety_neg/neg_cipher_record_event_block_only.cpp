// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Cipher::record_event refuses a context whose row holds Block and not
// IO.  With the IO-only fixture, this one makes each of the two atoms of
// the gate carry its own weight.

#include <crucible/Cipher.h>
#include <fixy/Ctx.h>

namespace eff = ::foundation::effects;

int main() {
    const ::fixy::TestRunnerCtx store_ctx{eff::testing::test()};
    auto cipher = ::crucible::Cipher::open(store_ctx, ::fixy::mint_tagged<::fixy::tags::source::External>(
                                                          std::filesystem::path{"/tmp/crucible_neg_record_event_block_only"}));
    const auto view = cipher.mint_open_view(store_ctx);
    const auto block_only = store_ctx.in_row<eff::Row<eff::Effect::Block>>();
    cipher.record_event(block_only, view, ::crucible::ContentHash{1u}, std::uint64_t{1u});
    return 0;
}
