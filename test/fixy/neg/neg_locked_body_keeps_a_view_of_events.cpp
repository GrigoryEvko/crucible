// A body of a Locked appends to a session event log, and a second body writes a
// View of events into a variable of the caller.  The store check of the door
// (StoresNoBorrowOutside of foundation/core/CaptureStore.h) reads only the
// events of a log, so the first body passes: a log is no borrow slot.  A View
// by reference is a slot that the caller reads after the frame, so the store
// check refuses the second body.

#include <fixy/os/SpinLock.h>
#include <fixy/session/EventLog.h>

#include <foundation/core/Region.h>
#include <foundation/effects/Effect.h>

#include <cstdint>

namespace eff = ::foundation::effects;
namespace s = ::fixy::session;

namespace {

struct CountTag final {};

struct Count {
    std::uint64_t value = 0;
};

using BlockCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Block>>;

}  // namespace

int main() {
    BlockCtx const ctx{eff::testing::bg()};
    ::fixy::spin::BlockingLocked<Count, CountTag> locked{};
    s::SessionEventLog log;
    locked.with(ctx, [&log](Count& count) noexcept {
        s::SessionEventWriter writer = s::mint_session_event_writer(eff::testing::test().alloc, log);
        writer.record_now(s::SessionEvent::close(s::RoleTagId{1}, s::RoleTagId{2}));
        count.value = log.size();
    });

    ::foundation::core::View<s::SessionEvent const> kept{};
    locked.with(ctx, [&kept](Count&) noexcept { kept = {}; });
    return static_cast<int>(kept.size());
}
