// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Cipher::commit_per_program refuses a bare region pointer.  The warm tier
// outlives one request, so the gate demands a band that states a scope of
// PER_PROGRAM or wider, and a bare pointer states none.

#include <crucible/Cipher.h>
#include <crucible/MetaLog.h>
#include <fixy/Ctx.h>

int main() {
    const ::fixy::TestRunnerCtx store_ctx{::foundation::effects::testing::test()};
    auto cipher =
        ::crucible::Cipher::open(store_ctx, ::fixy::mint_tagged<::fixy::tags::source::External>(
                                                std::filesystem::path{"/tmp/crucible_neg_program_bare_pointer"}));
    const auto view = cipher.mint_open_view(store_ctx);
    ::crucible::MetaLog log;

    const ::crucible::RegionNode* bare = nullptr;
    auto result = cipher.commit_per_program(view, bare, &log);
    return static_cast<int>(static_cast<bool>(std::move(result).consume()));
}
