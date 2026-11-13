// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Cipher::open refuses a path tagged Sanitized.  open runs the sanitizer
// itself, so the sanitized root it anchors to is always the result of its
// own check, never a caller's claim.  open takes a text tagged External,
// and a sanitized path is no such text.

#include <crucible/Cipher.h>
#include <fixy/Ctx.h>

#include <utility>

int main() {
    const ::fixy::TestRunnerCtx store_ctx{::foundation::effects::testing::test()};
    auto already_sanitized = ::fixy::sanitize_path(::fixy::mint_tagged<::fixy::tags::source::External>(
        std::filesystem::path{"/tmp/crucible_neg_open_sanitized_input"}));
    [[maybe_unused]] auto cipher =
        ::crucible::Cipher::open(store_ctx, store_ctx.cap().alloc, std::move(*already_sanitized));
    return 0;
}
