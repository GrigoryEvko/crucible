// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.

#include <crucible/forge/Ir001/Comm.h>

namespace ir = crucible::forge::ir001;

// A participant count is in [1, kIr001MaxParticipants].  The mint runs the
// range predicate, so a zero count stops the constant evaluation.
constexpr ir::Ir001ParticipantCount invalid_count = ::fixy::mint_refined<ir::kIr001ParticipantRange>(std::uint16_t{0});
