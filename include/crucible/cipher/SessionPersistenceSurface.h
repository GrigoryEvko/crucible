#pragma once

#include <fixy/ScopedView.h>
#include <fixy/os/ClockSource.h>
#include <fixy/os/Time.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

namespace crucible {

// The class is declared here and defined elsewhere. A consumer that
// names a reference to it, or a scoped view over it, needs no more,
// because the view stores one pointer. A consumer that calls a member
// function includes the defining header itself. The split exists
// because that header pulls in most of the persistence and graph
// stack, which every translation unit touching a persisted session
// would otherwise pay for.
class Cipher;

namespace cipher_state {
struct Open {};
}  // namespace cipher_state

using CipherOpenView = ::fixy::ScopedView<Cipher, cipher_state::Open>;

// The row is IO because the persistence path writes object files, and
// Block because it flushes them to storage at boundaries.
using CipherSessionEventPersistenceRow =
    ::foundation::effects::Row<::foundation::effects::Effect::IO, ::foundation::effects::Effect::Block>;

// The gate of every Cipher operation that reaches the store: a context
// whose row admits IO and Block.  A hot foreground context holds
// neither, so it opens no store and gets no view.
template <typename Ctx>
concept CtxFitsCipherPersistence = ::foundation::effects::CtxAdmits<Ctx, CipherSessionEventPersistenceRow>;

namespace cipher {

// The gate of a commit to the head log.  The commit writes and flushes
// files, so the context must fit the store.  The commit also stamps the
// log entry with a reading of the monotonic clock, so the context must
// fit the clock reader: its row owns Bg, Init or Test.  A clock read on
// the replay-bound foreground path makes replay diverge across machines.
template <typename Ctx>
concept CtxFitsCipherCommit = ::crucible::CtxFitsCipherPersistence<Ctx>
                           && ::fixy::time::CtxFitsClockReaderMint<Ctx, ::fixy::ClockSource_v::Monotonic>;

}  // namespace cipher

}  // namespace crucible
