// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule S001 (FIXY-V-243):
//
//     marks_hot_path<F>::value == true
//   ∧ F::type_t carries a StdioPinned tier >= BufferedWrite
//   ⇒ ill-formed
//
// Plain English: a hot-path function MUST NOT do stdio.  Format parsing
// is >= 100 ns and output syscalls flush buffers — TraceRing / Arena /
// KernelCache blow the hot-path budget (CLAUDE.md §XII bans stdio on the
// hot path; structured events go to an SPSC ring for bg drain instead).
//
// Mismatch class: hot-path marker × Stdio tier >= BufferedWrite.  Uses
// the WRAPPER-TIER trigger path (the shipped V-242 StdioPinned carrier
// pins BufferedWrite) plus the reused marks_hot_path marker — distinct
// from the ControlFlow-axis L006/P003 fixtures.
//
// Concrete bug-class this catches: dropping the `stdio_at_or_above_v<
// BufferedWrite, type_t>` term from S001_OK would let a hot-path function
// declare buffered stdio, silently re-introducing the syscall-flush
// latency spike on the recording path.
//
// Expected diagnostic substring: "S001:"

#include <crucible/safety/Fn.h>
#include <crucible/safety/Stdio.h>

namespace fn = crucible::safety::fn;
namespace sf = crucible::safety;
using SIO = crucible::algebra::lattices::Stdio;

namespace neg_collision_s001 {
using Bad = fn::Fn<sf::StdioPinned<SIO::BufferedWrite, int>>;  // Stdio tier >= BufferedWrite
}  // namespace neg_collision_s001

// Mark Bad as hot-path — required to fire S001 (the rule guards
// marks_hot_path AND a Stdio tier >= BufferedWrite).
namespace crucible::safety::fn::collision {
    template <> struct marks_hot_path<::neg_collision_s001::Bad>
        : std::true_type {};
}  // namespace crucible::safety::fn::collision

[[maybe_unused]] neg_collision_s001::Bad the_fixture{};

int main() { return 0; }
