#pragma once

#include <crucible/effects/_Capabilities.h>
#include <crucible/effects/_EffectRow.h>
#include <crucible/safety/_ScopedView.h>

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

using CipherOpenView = safety::ScopedView<Cipher, cipher_state::Open>;

// The row is IO because the persistence path writes object files, and
// Block because it flushes them to storage at boundaries.
using CipherSessionEventPersistenceRow =
    ::crucible::effects::Row<::crucible::effects::Effect::IO, ::crucible::effects::Effect::Block>;

}  // namespace crucible
