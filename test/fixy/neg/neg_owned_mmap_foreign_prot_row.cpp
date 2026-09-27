// A caller cannot add a protection of its own.  A door that reads the
// PROT_* word of a tag from a variable template maps whatever word a
// specialization writes, and this file writes PROT_WRITE | PROT_EXEC for
// a class of its own.  The PROT_* word of each tag is a row of a closed
// table in fixy/OwnedMmap.h, and no variable template holds it, so the
// specialization names nothing.

#include <fixy/os/Mmap.h>

#include <sys/mman.h>

namespace {
struct WriteExec final {};
}  // namespace

template <>
inline constexpr int fixy::mmap::prot_bits_v<WriteExec> = PROT_READ | PROT_WRITE | PROT_EXEC;

int main() {
    return 0;
}
