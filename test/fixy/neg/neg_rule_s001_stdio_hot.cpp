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

#include <fixy/Fn.h>

namespace {

struct log_rate_proved final {};

}  // namespace

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::stdio::write<::fixy::atom::stdio::streams::Stdout>,
                                ::fixy::atom::regime::hot, ::fixy::atom::cost_constant,
                                ::fixy::atom::refined_with<log_rate_proved>>
        refused{};
    return 0;
}
