// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.

#include <crucible/forge/Ir001/Comm.h>

namespace ir = crucible::forge::ir001;

constexpr ir::Ir001ParticipantCount invalid_count{0};
