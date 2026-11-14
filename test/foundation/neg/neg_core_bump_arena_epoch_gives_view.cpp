// The end of an epoch ends each byte that the arena gave out during the
// body, so a View of an array of the epoch must not leave it as the result.

#include <foundation/core/BumpArena.h>
#include <foundation/effects/Effect.h>

#include <cstdint>

int main() {
    ::foundation::core::BumpArena arena{4096};
    static_cast<void>(arena.with_epoch(
        [&]() noexcept { return arena.array<std::uint32_t>(::foundation::effects::testing::test().alloc, 4, 7u); }));
    return 0;
}
