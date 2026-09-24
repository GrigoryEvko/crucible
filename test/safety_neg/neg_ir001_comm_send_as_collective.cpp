// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.

#include <crucible/forge/Ir001/Comm.h>

namespace ir = crucible::forge::ir001;

template <ir::Ir001OpKind Kind>
    requires ir::Ir001CollectiveKind<Kind>
constexpr bool accepts_collective_kind() {
    return true;
}

static_assert(accepts_collective_kind<ir::Ir001OpKind::SendAsync>());
