// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Vigil::persist refuses a context narrowed to IO and Block.  persist
// stores the active region and commits the head, and the commit stamps
// the log entry with a reading of the monotonic clock.  The row owns none
// of Bg, Init and Test, so it does not fit the clock reader.

#include <crucible/Vigil.h>
#include <fixy/Ctx.h>

#include <utility>

namespace eff = ::foundation::effects;

using StoreOnlyCtx =
    decltype(std::declval<::fixy::TestRunnerCtx const&>().in_row<eff::Row<eff::Effect::IO, eff::Effect::Block>>());

using Refused = decltype(std::declval<::crucible::Vigil&>().persist(std::declval<StoreOnlyCtx const&>()));

int main() { return 0; }
