// A state that is not the brand holds a claim of the brand, to mint the
// brand's foreground context.  The claim's constructor is private, and the
// brand is its one friend, so the holder cannot build its member.

#include <foundation/effects/Ctx.h>

namespace {
struct Brand {};
struct Impostor {
    Impostor() noexcept {}
    ::foundation::effects::host::ProducerClaim<Brand> claim;
};
}  // namespace

int main() {
    Impostor impostor;
    (void)impostor;
    return 0;
}
