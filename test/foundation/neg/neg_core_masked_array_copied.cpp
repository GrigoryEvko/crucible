// A MaskedArray owns its storage alone, so a copy, which would free the
// storage two times, is refused.

#include <foundation/core/Array.h>
#include <foundation/effects/Effect.h>

int main() {
    ::foundation::core::MaskedArray<int> const first =
        ::foundation::core::mint_masked_array<int>(::foundation::effects::Alloc{}, 4);
    ::foundation::core::MaskedArray<int> const second = first;
    return static_cast<int>(second.size());
}
