// A producer claim built by a caller that is not its brand.  A claim with
// a public constructor let any thread build its own claim and hold a
// foreground context.  The constructor is private, and the brand is its
// one friend.

#include <foundation/effects/Ctx.h>

namespace {
struct Brand {};
}  // namespace

int main() {
    ::foundation::effects::host::ProducerClaim<Brand> claim;
    (void)claim;
    return 0;
}
