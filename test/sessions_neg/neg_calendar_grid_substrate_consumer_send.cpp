// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-083 fixture #1 - an indexed CalendarGrid consumer session is
// Recv-only.  The bridge must not expose send on the single drain
// ConsumerHandle.

#include <crucible/concurrent/SubstrateSessionBridge.h>
#include <crucible/permissions/Permission.h>
#include <crucible/safety/PermissionGridGenerator.h>
#include <crucible/sessions/CalendarGridSession.h>

#include <cstdint>
#include <tuple>
#include <utility>

namespace cc = crucible::concurrent;
namespace cgs = crucible::safety::proto::calendar_grid_session;

namespace {
struct Tag {};
struct Key {
    static std::uint64_t key(int value) noexcept {
        return static_cast<std::uint64_t>(value);
    }
};
using Grid = cc::PermissionedCalendarGrid<int, 2, 8, 16, Key, 1ULL, Tag>;
}

int main() {
    Grid grid;
    auto whole = crucible::safety::mint_permission_root<
        cc::calendar_tag::Whole<Tag>>();
    auto perms = crucible::safety::mint_grid_permissions<cc::calendar_tag::Whole<Tag>, 2, 1>(
        std::move(whole));
    auto consumer = grid.consumer(std::move(std::get<0>(perms.consumers)));
    auto psh = cc::mint_substrate_session<Grid, cc::CalendarConsumerId,
                                          cc::Direction::Consumer>(
        ::crucible::effects::HotFgCtx{}, consumer);
    [[maybe_unused]] auto bad = std::move(psh).send(7, cgs::blocking_push);
    return 0;
}
