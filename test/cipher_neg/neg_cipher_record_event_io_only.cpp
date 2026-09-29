// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Cipher::record_event refuses a context whose row holds IO and not
// Block.  A gate that asked for IO alone would still refuse the empty
// row, so this fixture is the one that holds the Block half of the gate.

#include <crucible/Cipher.h>
#include <fixy/Ctx.h>

namespace eff = ::foundation::effects;

int main() {
    const ::fixy::TestRunnerCtx store_ctx{eff::testing::test()};
    auto cipher =
        ::crucible::Cipher::open(store_ctx, ::fixy::mint_tagged<::fixy::tags::source::External>(
                                                std::filesystem::path{"/tmp/crucible_neg_record_event_io_only"}));
    const auto view = cipher.mint_open_view(store_ctx);
    const auto io_only = store_ctx.in_row<eff::Row<eff::Effect::IO>>();
    cipher.record_event(io_only, view, ::crucible::ContentHash{1u}, std::uint64_t{1u});
    return 0;
}
