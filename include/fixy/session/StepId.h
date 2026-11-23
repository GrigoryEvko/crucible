#pragma once

// The step of a session event: an id that does not decrease inside one log.
// The step of the log of a Vigil is also the step of its transactions
// (crucible/Transaction.h) and of its Cipher log (crucible/Cipher.h), so the
// three name one type.  The highest value of the word is the empty value of
// an Option of a step (foundation/core/Id.h), so no log mints it.

#include <foundation/core/Id.h>

#include <cstdint>

namespace fixy::session {

using StepId = ::foundation::core::Id<struct StepKind, std::uint64_t>;

}  // namespace fixy::session
