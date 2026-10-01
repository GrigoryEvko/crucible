// A caller cannot add a protection of its own.  A door that reads the
// PROT_* word of a tag from a variable template maps whatever word a
// specialization writes, and this file writes PROT_WRITE | PROT_EXEC for
// a class of its own.  The PROT_* word of each tag is a row of a closed
// table in fixy/OwnedMmap.h, and no variable template holds it, so the
// specialization names nothing.
//
// The static_assert names the gate of the table, and it shows that the
// class of this file has no row.  Without the header it is a second error,
// and the test runs with CRUCIBLE_NEG_STRICT_SINGLE_ERROR=1, so the fixture
// passes only when the specialization is its one error.

#include <fixy/os/Mmap.h>

#include <sys/mman.h>

namespace {
struct WriteExec final {};
}  // namespace

template <>
inline constexpr int fixy::mmap::prot_bits_v<WriteExec> = PROT_READ | PROT_WRITE | PROT_EXEC;

static_assert(!::fixy::mmap::MappedProt<WriteExec>);

int main() { return 0; }
