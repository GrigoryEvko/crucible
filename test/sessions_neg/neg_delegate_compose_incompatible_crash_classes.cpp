// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-049: a delegate-of-Stop carries the delegated endpoint's
// CrashClass into composition.  An Abort-grade stopped endpoint cannot
// satisfy a continuation position that explicitly requires NoThrow.

#include <crucible/sessions/SessionDelegate.h>

using namespace crucible::safety::proto;

namespace {

struct Ack {};

using BadComposition = compose_t<
    Delegate<Stop_g<CrashClass::Abort>, Recv<Ack, End>>,
    Stop_g<CrashClass::NoThrow>>;

[[maybe_unused]] BadComposition* should_not_compile = nullptr;

}  // namespace
