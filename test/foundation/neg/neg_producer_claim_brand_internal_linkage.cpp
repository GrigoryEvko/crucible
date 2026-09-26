// A claim of a brand with internal linkage.  A claim keys its brand in the
// process-wide registry by a stable identity, and a type with internal
// linkage has none: each translation unit holds its own entity under one
// printed name, so two brands would share one record.

#include <foundation/effects/Ctx.h>

namespace {
struct Hidden {
    ::foundation::effects::host::ProducerClaim<Hidden> claim;
};
}  // namespace

int main() {
    Hidden hidden;
    (void)hidden.claim.mint_producer_context();
    return 0;
}
