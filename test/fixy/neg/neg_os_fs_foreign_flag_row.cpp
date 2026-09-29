// A caller cannot add an open flag of its own.  A door that reads the O_*
// bits of a flag tag from a variable template opens with whatever bits a
// specialization writes, and this file writes O_TRUNC for a class of its
// own.  The O_* bits of each flag tag are a row of a closed table in
// fixy/os/Fs.h, and no variable template holds them, so the
// specialization names nothing.

#include <fixy/os/Fs.h>

#include <fcntl.h>

namespace {
struct Truncate final {};
}  // namespace

template <>
inline constexpr int fixy::fs::flag_bits_v<Truncate> = O_TRUNC;

int main() { return 0; }
