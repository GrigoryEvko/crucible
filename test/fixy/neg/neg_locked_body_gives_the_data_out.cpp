// A body of a Locked gives back the reference to the data that the lock lent
// it.  The caller would read and write the data after the lock is given
// back.  The door refuses a body whose result holds a borrow.

#include <fixy/os/SpinLock.h>

#include <foundation/effects/Effect.h>

#include <cstdint>

namespace eff = ::foundation::effects;

namespace {

struct CountTag final {};
using BlockCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Block>>;

}  // namespace

int main() {
    BlockCtx const ctx{eff::testing::bg()};
    ::fixy::spin::BlockingLocked<std::uint64_t, CountTag> locked{};
    std::uint64_t& escaped = locked.with(ctx, [](std::uint64_t& count) noexcept -> std::uint64_t& { return count; });
    return static_cast<int>(escaped);
}
