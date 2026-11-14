// The end of an epoch ends each byte that the arena gave out during the
// body, so a class that holds a View of the epoch must not leave it as the
// result.

#include <foundation/core/BumpArena.h>
#include <foundation/core/Region.h>
#include <foundation/effects/Effect.h>

#include <cstdint>

namespace {

struct Holder {
    ::foundation::core::View<std::uint32_t> elements{};
    std::uint32_t count = 0;
};

}  // namespace

int main() {
    ::foundation::core::BumpArena arena{4096};
    static_cast<void>(arena.with_epoch([&]() noexcept {
        return Holder{.elements = arena.array<std::uint32_t>(::foundation::effects::testing::test().alloc, 4, 7u),
                      .count = 4};
    }));
    return 0;
}
