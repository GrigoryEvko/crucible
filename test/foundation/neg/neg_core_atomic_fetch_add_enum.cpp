// A fetch-add steps an integer.  An addition to an enum has no meaning,
// so the operation is refused for an Atomic of an enum.

#include <foundation/core/Atomic.h>

#include <cstdint>

enum class Phase : std::uint8_t {
    idle,
    busy
};

int main() {
    ::foundation::core::Atomic<Phase> phase{Phase::idle};
    return static_cast<int>(phase.fetch_add_acq_rel(Phase::busy));
}
