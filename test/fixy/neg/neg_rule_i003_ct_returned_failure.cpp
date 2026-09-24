// I003: constant time x a failure the payload returns.
//
// The caller branches on the discriminant of the std::expected, so the
// failure is a branch on the classified data that leaves the
// constant-time region.  The error type is Secret, so I002 stands down,
// and the pack trips I003 alone.

#include <fixy/Fn.h>
#include <fixy/Secret.h>

#include <expected>

struct fault final {};

int main() {
    [[maybe_unused]] ::fixy::fn<std::expected<int, ::fixy::Secret<fault>>, ::fixy::atom::constant_time> refused{};
    return 0;
}
