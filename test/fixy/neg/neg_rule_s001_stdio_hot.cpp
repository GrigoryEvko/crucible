// S001: stdio x hot.
//
// Buffered stdio takes a lock and may block on a flush.  Neither belongs
// on a path budgeted in nanoseconds, which is why CLAUDE.md XII routes
// hot-path diagnostics through atomic counters and an SPSC ring and
// leaves the formatting to the background thread.
//
// S001 and P002 read the same Stdio axis from different sides: P002
// refuses a GHOST binding that emits, because erased code cannot write,
// and S001 refuses a HOT binding that emits, because the write is too
// slow.  Neither implies the other.
//
// A stdio write lifts IO and Block, so H003 and W001 refuse this pack
// too, and no pack can avoid them.  The fixture floors on the three
// codes together.  as_public is in the pack so that the corpus entry
// classified_io_without_declassify has no classified value to refuse.

#include <fixy/Fn.h>

// The tag has external linkage.  An atom whose argument has internal
// linkage has no stable identity, and the gate refuses it at tier 2.
namespace fixture {

struct log_rate_proved final {};

}  // namespace fixture

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::stdio::write<::fixy::atom::stdio::streams::Stdout>,
                                ::fixy::atom::regime::hot, ::fixy::atom::as_public, ::fixy::atom::cost_constant,
                                ::fixy::atom::refined_with<fixture::log_rate_proved>>
        refused{};
    return 0;
}
