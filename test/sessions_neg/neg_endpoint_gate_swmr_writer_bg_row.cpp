// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_writer_session over a single-writer multi-reader cell whose value
// carries the background row refuses the foreground context.  The writer
// publishes the value, so its protocol does not fit the context.

#include <crucible/effects/_Computation.h>
#include <crucible/sessions/SwmrSession.h>

namespace eff = ::crucible::effects;
namespace ses = ::crucible::safety::proto::swmr_session;

namespace {
struct WriterTag {};
struct ReaderTag {};
using BgInt = eff::Computation<eff::Row<eff::Effect::Bg>, int>;
using Swmr = ses::SwmrSession<BgInt, WriterTag, ReaderTag>;
}  // namespace

inline void mint_under_foreground(Swmr::WriterHandle& handle) {
    auto session = ses::mint_writer_session<Swmr>(eff::HotFgCtx{}, handle);
    (void)session;
}

int main() { return 0; }
