// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Cipher::record_event refuses the background drain context.  Bg names
// the background thread and does not imply IO or Block, and the drain row
// holds Bg and Alloc only.  A background thread that did not claim the two
// atoms in its row cannot record events.

#include <crucible/Cipher.h>
#include <fixy/Ctx.h>

namespace eff = ::foundation::effects;

int main() {
    const ::fixy::TestRunnerCtx store_ctx{eff::testing::test()};
    auto cipher = ::crucible::Cipher::open(store_ctx, ::fixy::mint_tagged<::fixy::tags::source::External>(
                                                          std::filesystem::path{"/tmp/crucible_neg_record_event_bg_only"}));
    const auto view = cipher.mint_open_view(store_ctx);
    const ::fixy::BgDrainCtx drain{eff::testing::bg()};
    cipher.record_event(drain, view, ::crucible::ContentHash{1u}, std::uint64_t{1u});
    return 0;
}
