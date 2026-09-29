// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Cipher::commit_per_fleet refuses a region whose lifetime band is
// PER_PROGRAM.  Program-scoped state, such as the scratch of one model
// instance, written to the fleet-wide cold tier would reach other
// instances on other relays, including a canary that runs another model
// version.  PER_FLEET sits above PER_PROGRAM, so the gate refuses the
// call.

#include <crucible/Arena.h>
#include <crucible/Cipher.h>
#include <crucible/MerkleDag.h>
#include <crucible/MetaLog.h>
#include <fixy/Bands.h>
#include <fixy/Ctx.h>

#include <utility>

int main() {
    const ::fixy::TestRunnerCtx store_ctx{::foundation::effects::testing::test()};
    auto cipher = ::crucible::Cipher::open(store_ctx, ::fixy::mint_tagged<::fixy::tags::source::External>(
                                                          std::filesystem::path{"/tmp/crucible_neg_program_at_fleet"}));
    const auto view = cipher.mint_open_view(store_ctx);
    ::crucible::MetaLog log;

    auto program_scoped =
        ::fixy::mint_band<::fixy::opaque_lifetime::PerProgram<const ::crucible::RegionNode*>>(nullptr);
    auto result = cipher.commit_per_fleet(view, std::move(program_scoped), &log);
    return static_cast<int>(static_cast<bool>(std::move(result).consume()));
}
