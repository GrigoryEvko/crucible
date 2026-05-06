// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-076: channel minting must carry both endpoint execution contexts.
// The old two-resource form is intentionally removed so a caller cannot
// bypass receiver-side row admission by relying on an implicit default Ctx.

#include <crucible/sessions/SessionMint.h>

namespace proto = crucible::safety::proto;

struct Resource {};

int main() {
    using Proto = proto::Send<int, proto::End>;

    [[maybe_unused]] auto channel =
        proto::mint_channel<Proto>(Resource{}, Resource{});
    return 0;
}
