// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_producer_session over a calendar grid whose job carries the background
// row refuses the foreground context.  The send side of the protocol does not
// fit a context that admits no background row.

#include <crucible/concurrent/_PermissionedCalendarGrid.h>
#include <crucible/effects/_Computation.h>
#include <crucible/sessions/_CalendarGridSession.h>

#include <cstdint>

namespace eff = ::crucible::effects;
namespace ses = ::crucible::safety::proto::calendar_grid_session;

namespace {
struct Tag {};
using BgInt = eff::Computation<eff::Row<eff::Effect::Bg>, int>;
struct Key {
    static std::uint64_t key(BgInt const&) noexcept { return 0; }
};
using Grid = ::crucible::concurrent::PermissionedCalendarGrid<BgInt, 2, 8, 16, Key, 1000000ULL, Tag>;
}  // namespace

inline void mint_under_foreground(Grid::ProducerHandle<0>& handle) {
    auto session = ses::mint_producer_session<Grid, 0>(eff::HotFgCtx{}, handle);
    (void)session;
}

int main() { return 0; }
