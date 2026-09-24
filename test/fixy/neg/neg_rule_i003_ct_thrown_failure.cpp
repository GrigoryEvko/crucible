// I003: constant time x a failure the body throws.
//
// The second mismatch class: the failure path is the ControlFlow grade
// ctrl::throws<E> rather than the payload.  The exception family is
// Secret, so I002 stands down, and the pack trips I003 alone.

#include <fixy/Fn.h>
#include <fixy/Secret.h>

struct fault final {};

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::constant_time, ::fixy::atom::ctrl::throws<::fixy::Secret<fault>>>
        refused{};
    return 0;
}
