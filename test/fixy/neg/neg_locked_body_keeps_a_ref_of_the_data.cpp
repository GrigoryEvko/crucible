// A body of a Locked keeps a Ref of the data in a variable that it captures
// by reference.  The Ref outlives the frame of the lock, so the caller would
// read the data after the unlock while the next holder writes it: a data
// race.  The store check of the door (StoresNoBorrowOutside of
// foundation/core/CaptureStore.h) refuses the body.

#include <fixy/os/SpinLock.h>

#include <foundation/core/Choice.h>
#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

#include <cstdint>

namespace eff = ::foundation::effects;

namespace {

struct CountTag final {};
struct CountBrand final {};

struct Count {
    using brand_type = CountBrand;
    std::uint64_t value = 0;
};

using BlockCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Block>>;

}  // namespace

int main() {
    BlockCtx const ctx{eff::testing::bg()};
    ::fixy::spin::BlockingLocked<Count, CountTag> locked{};
    ::foundation::core::Option<::foundation::core::Ref<Count, CountBrand>> kept{::foundation::core::none};
    locked.with(ctx,
                [&kept](Count& count) noexcept { kept = ::foundation::core::some(::foundation::core::to(count)); });
    return kept.is_some() ? 0 : 1;
}
