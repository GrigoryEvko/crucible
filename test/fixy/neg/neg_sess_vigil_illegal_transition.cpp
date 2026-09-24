// The legal mode transitions are the edges declared in
// vigil_mode::admitted_mode_transitions, and DIVERGED is in neither
// direction: it is a replay status rather than a persistent mode.
//
// Naming a pair the namespace does not declare is refused where the
// transition type is formed, so the rejection arrives before any
// session carries the value.

#include <fixy/session/VigilMode.h>

namespace vm = fixy::session::vigil_mode;

// Both modes exist and the type is well formed; only the relation
// refuses the pair.
using Forbidden = vm::ModeTransition<vm::Mode::RECORDING, vm::Mode::DIVERGED>;

int main() { return sizeof(Forbidden) == 0 ? 1 : 0; }
