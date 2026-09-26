#pragma once

#include <fixy/ScopedView.h>
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

}  // namespace crucible
