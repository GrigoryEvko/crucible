// mint_vigil_mode_bridge is gated on the cell being the Vigil mode cell
// and nothing else, so a call with an unrelated type is refused by name
// rather than as a substitution failure deep inside the substrate.

#include <fixy/session/VigilMode.h>

#include <cstdint>

namespace vm = fixy::session::vigil_mode;

int main() {
    std::uint64_t not_a_cell = 0;
    auto session = vm::mint_vigil_mode_bridge(not_a_cell);
    (void)session;
    return 0;
}
