// V301: hot x a fence at or above SeqCst.
//
// A sequentially consistent fence drains the store buffer, which is
// ~30 ns on x86 — the whole of a hot-path budget spent on one
// instruction.  CLAUDE.md IX's discipline is acquire and release only on
// the hot path, and that is what this rule holds.
//
// The old catalog's predicate is "at or above SeqCst", so FullFence trips
// it too; the pending_rules text said "full fence" and the port keeps
// the wider, older reading.  The pack uses seq_cst rather than
// full_fence so the boundary itself is what the fixture exercises.

#include <fixy/Fn.h>

// The tag has external linkage.  An atom whose argument has internal
// linkage has no stable identity, and the gate refuses it at tier 2.
namespace fixture {

struct ring_index_proved final {};

}  // namespace fixture

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::regime::hot, ::fixy::atom::barrier::seq_cst,
                                ::fixy::atom::cost_constant, ::fixy::atom::refined_with<fixture::ring_index_proved>>
        refused{};
    return 0;
}
