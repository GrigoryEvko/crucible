// H010: hot x Row<Bg>.
//
// A function cannot be both on the foreground hot path and in background
// context: the two name different threads.  CLAUDE.md IX gives the
// foreground exactly one job, recording at the ring, and the background
// everything else.
//
// H010 exists because H001 and H003 both miss this pack.  The cost is
// constant, so H001 stands down; there is no Alloc or IO row, so H003
// stands down; and the binding is still a contradiction.  That is the
// whole reason the rule is separate, and this fixture is the pack that
// shows it — the two atoms that silence H001 and H002 are in it
// deliberately.

#include <fixy/Fn.h>

#include <foundation/effects/Effect.h>

// The tag has external linkage.  An atom whose argument has internal
// linkage has no stable identity, and the gate refuses it at tier 2.
namespace fixture {

struct queue_depth_proved final {};

}  // namespace fixture

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::regime::hot,
                                ::fixy::atom::with<::foundation::effects::Effect::Bg>,
                                ::fixy::atom::cost_constant, ::fixy::atom::refined_with<fixture::queue_depth_proved>>
        refused{};
    return 0;
}
