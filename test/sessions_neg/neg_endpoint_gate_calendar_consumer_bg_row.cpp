// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_consumer_session over a calendar grid whose job carries the background
// row refuses the foreground context.  The receive side of the protocol brings
// the row into the receiver, which the context does not admit.

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

inline void mint_under_foreground(Grid::ConsumerHandle& handle) {
    auto session = ses::mint_consumer_session<Grid>(eff::HotFgCtx{}, handle);
    (void)session;
}

int main() { return 0; }
