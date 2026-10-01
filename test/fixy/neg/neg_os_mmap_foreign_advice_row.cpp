// A caller cannot add an advice of its own.  A door that reads the MADV_*
// value of a tag from a variable template passes whatever value a
// specialization writes to madvise, and this file writes MADV_DONTNEED
// for a class of its own.  The MADV_* value of each tag is a row of a
// closed table in fixy/os/Mmap.h, and no variable template holds it, so
// the specialization names nothing.
//
// The static_assert names the gate of the header, and it shows that the
// class of this file has no row.  Without the header it is a second error,
// and the test runs with CRUCIBLE_NEG_STRICT_SINGLE_ERROR=1, so the fixture
// passes only when the specialization is its one error.

#include <fixy/os/Mmap.h>

#include <sys/mman.h>

namespace {
struct Discard final {};
}  // namespace

template <>
inline constexpr int fixy::mmap::advice_value_v<Discard> = MADV_DONTNEED;

static_assert(!::fixy::mmap::KnownAdvice<Discard>);

int main() { return 0; }
