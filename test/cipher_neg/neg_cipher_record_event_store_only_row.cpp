// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Cipher::record_event refuses a context narrowed to IO and Block.  The
// row fits the store, but it owns none of Bg, Init and Test, so it does
// not fit the monotonic clock reader that stamps the log entry.  A
// reading of the clock on the replay-bound foreground path makes replay
// diverge across machines.

#include <crucible/Cipher.h>
#include <fixy/Ctx.h>

#include <cstdint>
#include <utility>

namespace eff = ::foundation::effects;

using StoreOnlyCtx =
    decltype(std::declval<::fixy::TestRunnerCtx const&>().in_row<eff::Row<eff::Effect::IO, eff::Effect::Block>>());

using Refused = decltype(std::declval<::crucible::Cipher&>().record_event(
    std::declval<StoreOnlyCtx const&>(), std::declval<::crucible::Cipher::OpenView const&>(),
    ::crucible::ContentHash{1u}, std::uint64_t{1u}));

int main() { return 0; }
